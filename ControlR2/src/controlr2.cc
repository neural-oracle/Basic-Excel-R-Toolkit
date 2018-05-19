
#include "controlr2.h"
#include "json11/json11.hpp"
#include "process_exit_codes.h"
#include "controlr2_interface.h"

#include <iostream>
#include <deque>

#ifdef min
#undef min
#endif

// FIXME: remove these or make them class members (and make this a class)
std::string language_tag;
std::string rhome;
std::string pipename;
std::string process_name;

uv_timer_t wait_timeout;

const int32_t Timeout = 100;

int pipe_count = 0;
int console_client = -1;

typedef struct {
  uint32_t index;
  std::string buffer;
} PipeData;

typedef struct {
  uint32_t client_id;
  BERTBuffers::CallResponse message;
} 
ClientMessage;

typedef struct {
  uv_write_t req;
  uv_buf_t buf;
} 
WriteRequest;

std::vector <uv_pipe_t*> clients_;
//std::vector <ClientMessage> pending_messages_;
std::deque<ClientMessage> pending_messages_;
std::vector <std::string> console_buffer_;

// fwd: FIXME

void Write(const std::string &data, uv_pipe_t *client);


// dummy
bool Callback(const BERTBuffers::CallResponse &call, BERTBuffers::CallResponse &response) {
  return true;
}

// dummy 
bool ConsoleCallback(const BERTBuffers::CallResponse &call, BERTBuffers::CallResponse &response) {
  return true;
}

void QueueConsoleWrites() {
  uv_pipe_t *client = clients_[console_client];
  for (auto message : console_buffer_) {
    Write(message, client);
  }
  console_buffer_.clear();
}

/**
 * in an effort to make the core language agnostic, all actual functions are moved
 * here. this should cover things like initialization and setting the COM pointers.
 *
 * the caller uses symbolic constants that call these functions in the appropriate
 * language.
 */
int SystemCall(BERTBuffers::CallResponse &response, const BERTBuffers::CallResponse &call, int pipe_index) {
  std::string function = call.function_call().function();

  BERTBuffers::CallResponse translated_call;
  translated_call.CopyFrom(call);

  /*
  if (!function.compare("install-application-pointer")) {
    translated_call.mutable_function_call()->set_target(BERTBuffers::CallTarget::language);
    translated_call.mutable_function_call()->set_function("BERT$install.application.pointer");
    RCall(response, translated_call);
  }
  else if (!function.compare("list-functions")) {
    response.set_id(call.id());
    ListScriptFunctions(response);
  }
  else if (!function.compare("get-language")) {
    response.mutable_result()->set_str(language_tag); //  "R");
  }
  else if (!function.compare("read-source-file")) {
    std::string file = call.function_call().arguments(0).str();
    bool notify = false;
    if (call.function_call().arguments_size() > 1) notify = call.function_call().arguments(1).boolean();
    bool success = false;
    if (file.length()) {
      std::cout << "read source: " << file << " (" << (notify ? "notify" : "silent") << ")" << std::endl;
      success = ReadSourceFile(file, notify);
    }
    response.mutable_result()->set_boolean(success);
  }
  else 
  */
  if (!function.compare("shutdown")) {
    // ConsoleControlMessage("shutdown");
    // Shutdown(0);
    return SYSTEMCALL_SHUTDOWN;
  }
  /*
  else if (!function.compare("close")) {
    CloseClient(pipe_index);
    return SYSTEMCALL_OK; //  break; // no response?
  }
  */
  else if (!function.compare("console")) {
    if (console_client < 0) {
      console_client = pipe_index;
      std::cout << "set console client -> " << pipe_index << std::endl;
      QueueConsoleWrites();
    }
    else std::cerr << "console client already set" << std::endl;
  }
  else {
    std::cout << "ENOTIMPL (system): " << function << std::endl;
    response.mutable_result()->set_boolean(false);
  }

  return SYSTEMCALL_OK;

}

void on_write(uv_write_t *req, int status) {
  if(status) std::cout << "on_write status " << status << std::endl;
  WriteRequest *wr = (WriteRequest*)req;
  free(wr->buf.base);
  free(wr);
}

void Write(const std::string &data, uv_pipe_t *client) {
  WriteRequest *req = (WriteRequest*)malloc(sizeof(WriteRequest));
  req->buf = uv_buf_init((char*)malloc(data.length()), data.length());
  memcpy(req->buf.base, data.c_str(), data.length());
  int r = uv_write((uv_write_t*)req, (uv_stream_t*)client, &(req->buf), 1, on_write);
  if(r) std::cout << "write returned " << r << ", err? " << uv_err_name(r) << std::endl;
}

void ConsolePrompt(const char *prompt, uint32_t id) {
  BERTBuffers::CallResponse message;
  message.set_id(id);
  message.mutable_console()->set_prompt(prompt);
  PushConsoleMessage(message);
}

int InputStreamRead(const char *prompt, char *buf, int len, int addtohistory, bool is_continuation) {

  static uint32_t prompt_transaction_id = 0;

  std::cout << "ISR start" << std::endl;
  uv_loop_t *loop = uv_default_loop();

  ConsolePrompt(prompt, prompt_transaction_id);

  while (true) {

    // reset timeout
    int r = uv_timer_again(&wait_timeout);

    // run the loop, until we get an event or timeout that we need to break on
    uv_run(loop, UV_RUN_DEFAULT);

    if(pending_messages_.size()) {
      //for (auto client_message : pending_messages_) 
      while(pending_messages_.size())
      {
        auto client_message = pending_messages_[0];
        pending_messages_.pop_front();

        BERTBuffers::CallResponse response;
        BERTBuffers::CallResponse &call = client_message.message;

        response.set_id(call.id());
        switch (call.operation_case()) {

        case BERTBuffers::CallResponse::kFunctionCall:
          //call_depth++;
          //active_pipe.push(index);
          switch (call.function_call().target()) {
          case BERTBuffers::CallTarget::system:
            if (SYSTEMCALL_SHUTDOWN == SystemCall(response, call, client_message.client_id)) {
              //Shutdown(0); // we're not handling this well
              return 0; // will terminate R loop
            }
            break;
          default:
            RCall(response, call);
            break;
          }
          //active_pipe.pop();
          //call_depth--;
          if (call.wait()) {
            Write(MessageUtilities::Frame(response), clients_[client_message.client_id]);
            //pipe->PushWrite(MessageUtilities::Frame(response));
          }
          break;

        case BERTBuffers::CallResponse::kCode:
          //call_depth++;
          //active_pipe.push(index);
          RExec(response, call);
          //active_pipe.pop();
          //call_depth--;
          //if (call.wait()) pipe->PushWrite(MessageUtilities::Frame(response));
          if (call.wait()) {
            Write(MessageUtilities::Frame(response), clients_[client_message.client_id]);
          }
          break;

        case BERTBuffers::CallResponse::kShellCommand:
          len = std::min(len - 2, (int)call.shell_command().length());
          //strcpy_s(buf, len + 1, call.shell_command().c_str());
          memcpy(buf, call.shell_command().c_str(), len+1);
          buf[len++] = '\n';
          buf[len++] = 0;
          prompt_transaction_id = call.id();
          //pipe->StartRead();


          // start read and then exit this function; that will cycle the R REPL loop.
          // the (implicit/explicit) response from this command is going to be the next 
          // prompt.

          return len;
        default:
          std::cout << "unhandled case: " << call.operation_case() << std::endl;
          break;
        }
      }
      //pending_messages_.clear();
    }
    else {
      RTick();
      UpdateSpreadsheetGraphics();
    }

  }

  std::cout << "ISR end" << std::endl;

  return 0;
}

void PushConsoleMessage(google::protobuf::Message &message) {
  std::string framed = MessageUtilities::Frame(message);
  if (console_client >= 0) {
    Write(framed, clients_[console_client]);
  }
  else {
    console_buffer_.push_back(framed);
  }
}

void ConsoleMessage(const char *buf, int len, int flag) {
  BERTBuffers::CallResponse message;
  if (flag) message.mutable_console()->set_err(buf);
  else message.mutable_console()->set_text(buf);
  PushConsoleMessage(message);
}

void StartR() {
  char process[64];
  
  int len = process_name.length();
  if( len > 63 ) len = 63;
  memcpy(process, process_name.c_str(), len);
  process[len] = 0;

  std::cout << "StartR" << std::endl;

  char* args[] = { process, "--no-save", "--no-restore", "--encoding=UTF-8" };
  int result = RLoop(rhome.c_str(), "", 4, args);

  std::cout << "r loop end" << std::endl;

}

void on_timeout(uv_timer_t *timer) {
  uv_loop_t *loop = uv_default_loop();
  // std::cout << "timeout!" << std::endl;
  uv_stop(loop);
}

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  *buf = uv_buf_init((char*)malloc(suggested_size), suggested_size);
}

void Read(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {

  uv_loop_t *loop = uv_default_loop();
  PipeData *data = static_cast<PipeData*>(stream->data);

  if (nread < 0) std::cerr << "on_read error [" << data->index << "] " << nread << ": " << uv_err_name(nread) << std::endl;
  else {
    BERTBuffers::CallResponse message;
    bool success;
    std::string str;
    
    // FIXME: this unframe doesn't account for extra bytes, so we either need to
    // rewrite it or write a new one (or we could do it in here, but probably not)

    if (data->buffer.length()) str = data->buffer;
    str.append(buf->base, nread);
    success = MessageUtilities::Unframe(message, str);
    if (success) {
      uv_stop(loop);
      data->buffer = "";
      ClientMessage cm;
      cm.client_id = data->index;
      cm.message = message;
      pending_messages_.push_back(cm);
    }
    else {
      std::cout << "onread [" << data->index << "]: " << nread << ", message failed; buffering" << std::endl;
      data->buffer = str;
    }
  }

  free(buf->base);
  
}

void on_new_connection(uv_stream_t *server, int status) {

  uv_loop_t *loop = uv_default_loop();

  if (status == -1) {
    std::cerr << "on_new_connection status = -1" << std::endl;
    // error!
    return;
  }

  uv_pipe_t *client = (uv_pipe_t*)malloc(sizeof(uv_pipe_t));

  uv_pipe_init(loop, client, 0);

  PipeData *data = new PipeData;
  data->index = pipe_count++;
  client->data = static_cast<void*>(data);
  clients_.push_back(client);

  // uv_tcp_init(loop, client);
  if (uv_accept(server, (uv_stream_t*)client) == 0) {
    std::cout << "accept ok" << std::endl;
    uv_read_start((uv_stream_t*)client, alloc_buffer, Read);
  }
  else {
    std::cout << "accept fail, closing" << std::endl;
    uv_close((uv_handle_t*)client, NULL);
  }
}

/**
 * removes old unix domain socket, if it exists. it should not,
 * since we're pid-specific, and if it's overwriting an existing
 * socket this should fail (correctly).
 */
void RemoveDomainSocketFile(const std::string &name){
  int result = unlink(name.c_str());
  if(result) std::cerr << "err in unlink: " << uv_err_name(result) << std::endl;
}

std::string DecoratePipeName(const std::string &base_name) {
#ifdef _WIN32
  std::string name = "\\\\.\\pipe\\";
#else
  std::string name = "/tmp/";
#endif
  name += base_name;
  return name;
}

void ManagementThreadRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {

  PipeData *data = static_cast<PipeData*>(stream->data);

  if (nread < 0) std::cerr << "ManagementThreadRead error [" << data->index << "] " << nread << ": " << uv_err_name(nread) << std::endl;
  else {
    BERTBuffers::CallResponse message;
    bool success;
    std::string str;

    // FIXME: this unframe doesn't account for extra bytes, so we either need to
    // rewrite it or write a new one (or we could do it in here, but probably not)

    // NOTE: and this is copied-and-pasted, so now there are two

    // unlike the other read, we're handling inline. but with a function 
    // pointer or lambda we could unify the two functions.

    if (data->buffer.length()) str = data->buffer;
    str.append(buf->base, nread);
    success = MessageUtilities::Unframe(message, str);
    if (success) {
      std::string command = message.function_call().function();
      if (command.length()) {
        if (!command.compare("break")) {
          //user_break_flag = true; // ?
          std::cout << "Set user break!" << std::endl;
          RSetUserBreak();
        }
        else {
          std::cerr << "unexpected system command (management pipe): " << command << std::endl;
        }
      }
      data->buffer = "";
    }
    else {
      std::cout << "ManagementThreadRead: " << nread << ", message failed; buffering" << std::endl;
      data->buffer = str;
    }
  }

  free(buf->base);

}

void ManagementThreadConnect(uv_stream_t *server, int status) {

  // FIXME: this is identical to the other connect function, 
  // except for the different loop and that we are not using 
  // the client index (or storing the client in the list).
  // MERGE. (use test server->data as loop === default loop)

  uv_loop_t *loop = static_cast<uv_loop_t*>(server->data);
  if (status == -1) {
    std::cerr << "status err in connect (management thread)" << std::endl;
    return;
  }

  uv_pipe_t *client = (uv_pipe_t*)malloc(sizeof(uv_pipe_t));
  uv_pipe_init(loop, client, 0);

  PipeData *data = new PipeData;
  data->index = 0;
  client->data = static_cast<void*>(data);

  if (uv_accept(server, (uv_stream_t*)client) == 0) {
    std::cout << "ManagementThreadConnect: accept ok" << std::endl;
    uv_read_start((uv_stream_t*)client, alloc_buffer, ManagementThreadRead);
  }
  else {
    std::cout << "ManagementThreadConnect: accept fail, closing" << std::endl;
    uv_close((uv_handle_t*)client, NULL);
  }

}

void ManagementThread(void *arg) {

  uv_loop_t management_loop;
  uv_loop_init(&management_loop);

  uv_pipe_t pipe_server;
  uv_pipe_init(&management_loop, &pipe_server, 0);

  std::string *pipe_name = static_cast<std::string*>(arg);
  std::cerr << "mgmt pipe " << pipe_name->c_str() << std::endl;

  int r = 0;

  std::string full_name = DecoratePipeName(*pipe_name);

#ifndef _WIN32
  RemoveDomainSocketFile(full_name);
#endif

  if ((r = uv_pipe_bind(&pipe_server, full_name.c_str()))) {
    std::cerr << "Bind error " << uv_err_name(r) << std::endl;
  }

  // we need the loop but we can't capture, so store it in the data field
  pipe_server.data = static_cast<void*>(&management_loop);
  
  if (!r) {
    if ((r = uv_listen((uv_stream_t*)&pipe_server, 128, ManagementThreadConnect))) {
      std::cerr << "Listen error " << uv_err_name(r) << std::endl;
    }
  }

  uv_run(&management_loop, UV_RUN_DEFAULT);

  std::cerr << "end management thread" << std::endl;
  uv_loop_close(&management_loop);
}

int main(int argc, char **argv) {
  
  // char buffer[MAX_PATH];
  int major, minor, patch;

  RGetVersion(&major, &minor, &patch);

  std::cout << "R version: " << major << ", " << minor << ", " << patch << std::endl;

  if (major != 3) return PROCESS_ERROR_UNSUPPORTED_VERSION;
  if (minor != 4) return PROCESS_ERROR_UNSUPPORTED_VERSION;

  std::stringstream ss;
  ss << "R::" << major << "." << minor << "." << patch;
  language_tag = ss.str();

  for (int i = 0; i < argc; i++) {
    if (!strncmp(argv[i], "-p", 2) && i < argc - 1) {
      pipename = argv[++i];
    }
    else if (!strncmp(argv[i], "-r", 2) && i < argc - 1) {
      rhome = argv[++i];
    }
  }

  if (!pipename.length()) {
    std::cerr << "call with -p pipename" << std::endl;
    //MessageBoxA(0, "missing pipe name", "R ERR", MB_OK);
    return PROCESS_ERROR_CONFIGURATION_ERROR;
  }
  if (!rhome.length()) {
    std::cerr << "call with -r rhome" << std::endl;
    //MessageBoxA(0, "missing r name", "R ERR", MB_OK);
    return PROCESS_ERROR_CONFIGURATION_ERROR;
  }

  std::string management_pipe_name = pipename;
  management_pipe_name += "-M";
  uv_thread_t management_thread_handle;
  uv_thread_create(&management_thread_handle, ManagementThread, static_cast<void*>(&management_pipe_name));

  uv_loop_t *loop = uv_default_loop();
  uv_pipe_t server;
  uv_pipe_init(loop, &server, 0);

  // FIXME: signal handler, at least for non-windows

  process_name = argv[0];

  int r;
  std::cout << "creating pipe" << std::endl;
  std::string full_name = DecoratePipeName(pipename);

  uv_timer_init(loop, &wait_timeout);
  uv_timer_start(&wait_timeout, [](uv_timer_t *timer) {
      uv_stop(uv_default_loop()); 
    }, Timeout, Timeout);

#ifndef _WIN32
  RemoveDomainSocketFile(full_name);
#endif

  if ((r = uv_pipe_bind(&server, full_name.c_str()))) {
    std::cerr << "Bind error " << uv_err_name(r) << std::endl;
    return 1;
  }
  if ((r = uv_listen((uv_stream_t*)&server, 128, on_new_connection))) {
    std::cerr << "Listen error " << uv_err_name(r) << std::endl;
    return 2;
  }
  // return uv_run(loop, UV_RUN_DEFAULT);

  StartR();

  std::cout << "exiting" << std::endl;
  
  return 0;
}
