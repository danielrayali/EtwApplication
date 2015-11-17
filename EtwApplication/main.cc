#define INITGUID  // Include this #define to use SystemTraceControlGuid in Evntrace.h.

#include <exception>
#include <iostream>
#include <cstddef>
#include <string>
#include <sstream>

#include <windows.h>
#include <evntrace.h>
#include <Evntcons.h>

using namespace std;

static TRACEHANDLE trace_handle{ NULL };

EVENT_TRACE_PROPERTIES* ConfigureEventTraceProperties() {
  size_t size = sizeof(EVENT_TRACE_PROPERTIES) + sizeof(KERNEL_LOGGER_NAMEA);
  EVENT_TRACE_PROPERTIES* properties = static_cast<EVENT_TRACE_PROPERTIES*>(malloc(size));
  ZeroMemory(properties, size);

  // Setup Wnode of properties
  properties->Wnode.BufferSize = sizeof(EVENT_TRACE_PROPERTIES) + sizeof(KERNEL_LOGGER_NAMEA);
  properties->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
  properties->Wnode.Guid = SystemTraceControlGuid;

  // Setup rest of properties
  properties->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
  properties->FlushTimer = 1;
  properties->EnableFlags = (EVENT_TRACE_FLAG_DISK_IO | EVENT_TRACE_FLAG_DISK_IO_INIT | EVENT_TRACE_FLAG_FILE_IO_INIT);
  properties->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

  // Return properties
  return properties;
}

ULONG WINAPI BufferCallback(PEVENT_TRACE_LOGFILE buffer) {
  cout << "BufferCallback called" << endl;
  return TRUE;
}

VOID WINAPI EventRecordCallback(PEVENT_RECORD record) {
  cout << "EventRecordCallback called" << endl;
}

void ConfigureEventTraceLogfile(EVENT_TRACE_LOGFILE& log_file) {
  log_file.LoggerName = KERNEL_LOGGER_NAMEA;
  log_file.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME;
  log_file.BufferCallback = &BufferCallback;
}

FILETIME GetCurrentTimeAsFileTime() {
  FILETIME file_time = { 0 };
  SYSTEMTIME sys_time = { 0 };

  GetSystemTime(&sys_time);
  if (!SystemTimeToFileTime(&sys_time, &file_time)) {
    throw exception("Couldn't convert system time to file time");
  }

  return file_time;
}

BOOL CtrlHandler(DWORD fdwCtrlType)
{
  cout << "Calling CloseTrace" << endl;
  ULONG rc = CloseTrace(trace_handle);
  if ((rc != ERROR_CTX_CLOSE_PENDING) && (rc != ERROR_SUCCESS)) {
    cerr << "CloseTrace failed: " << rc << endl;
  }
  return TRUE;
}

int main(int argc, char* argv[]) {
  EVENT_TRACE_PROPERTIES* properties = nullptr;
  try {
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)CtrlHandler, TRUE)) { throw exception("Couldn't register control handler"); }

    properties = ConfigureEventTraceProperties();

    TRACEHANDLE session_handle = 0;
    ULONG rc = StartTrace(&session_handle, KERNEL_LOGGER_NAMEA, properties);
    if (rc == ERROR_ALREADY_EXISTS) {
      // This stops the trace, the antithesis of StartTrace
      rc = ControlTrace(session_handle, KERNEL_LOGGER_NAMEA, properties, EVENT_TRACE_CONTROL_STOP);
      if (rc != ERROR_SUCCESS) {
        string rc_str = to_string(rc);
        throw exception(string("ControlTrace failed with error code: ").append(rc_str).c_str());
      }
    } else if (rc != ERROR_SUCCESS) {
      stringstream error;
      error << "StartTrace failed with error code: " << rc;
      throw exception(error.str().c_str());
    }

    EVENT_TRACE_LOGFILE log_file = { 0 };
    ConfigureEventTraceLogfile(log_file);

    trace_handle = OpenTrace(&log_file);
    if (trace_handle == INVALID_PROCESSTRACE_HANDLE) {
      stringstream error;
      error << "Couldn't open the trace. GetLastError: " << GetLastError();
      throw exception(error.str().c_str());
    }

    FILETIME start_time = GetCurrentTimeAsFileTime();
    FILETIME end_time = { 0xffffffff, 0xffffffff };
    rc = ProcessTrace(&trace_handle, 1, &start_time, &end_time);
    while (rc == ERROR_WMI_INSTANCE_NOT_FOUND) {
      rc = CloseTrace(trace_handle);
      if (rc != ERROR_SUCCESS) {
        stringstream error;
        error << "CloseTrace failed while trying to restart the trace: " << rc;
        throw exception(error.str().c_str());
      }

      session_handle = 0;
      rc = StartTrace(&session_handle, KERNEL_LOGGER_NAMEA, properties);
      if (rc != ERROR_SUCCESS) {
        stringstream error;
        error << "StartTrace failed with error code: " << rc;
        throw exception(error.str().c_str());
      }

      trace_handle = OpenTrace(&log_file);
      if (trace_handle == INVALID_PROCESSTRACE_HANDLE) {
        stringstream error;
        error << "OpenTrace filed while trying to restart the trace. GetLastError: " << GetLastError();
        throw exception(error.str().c_str());
      }

      rc = ProcessTrace(&trace_handle, 1, &start_time, &end_time);
      cout << "Loop" << endl;
    }
    
    if (rc != ERROR_SUCCESS) {
      stringstream error;
      error << "ProcessTrace failed. GetLastError: " << rc << endl;
      throw exception(error.str().c_str());
    }

    cout << "Stopping trace" << endl;

    // This stops the trace, the antithesis of StartTrace
    rc = ControlTrace(session_handle, KERNEL_LOGGER_NAMEA, properties, EVENT_TRACE_CONTROL_STOP);
    if (rc != ERROR_SUCCESS) {
      string rc_str = to_string(rc);
      throw exception(string("ControlTrace failed with error code: ").append(rc_str).c_str());
    }

    free(properties);
    return 0;
  }
  catch (exception& ex) {
    cerr << "Error: " << ex.what() << endl;
    free(properties);
    return 1;
  }
}