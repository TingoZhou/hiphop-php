/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010-2013 Facebook, Inc. (http://www.facebook.com)     |
   | Copyright (c) 1998-2010 Zend Technologies Ltd. (http://www.zend.com) |
   +----------------------------------------------------------------------+
   | This source file is subject to version 2.00 of the Zend license,     |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.zend.com/license/2_00.txt.                                |
   | If you did not receive a copy of the Zend license and are unable to  |
   | obtain it through the world-wide-web, please send a note to          |
   | license@zend.com so we can mail you a copy immediately.              |
   +----------------------------------------------------------------------+
*/

#include "hphp/runtime/base/stream_wrapper_registry.h"
#include "hphp/runtime/base/file.h"
#include "hphp/runtime/base/string_util.h"
#include "hphp/runtime/base/file_stream_wrapper.h"
#include "hphp/runtime/base/php_stream_wrapper.h"
#include "hphp/runtime/base/http_stream_wrapper.h"
#include "hphp/runtime/base/request_local.h"
#include <set>
#include <map>

namespace HPHP { namespace Stream {
///////////////////////////////////////////////////////////////////////////////

namespace {
class RequestWrappers : RequestEventHandler {
 public:
  virtual void requestInit() {}
  virtual void requestShutdown() {
    m_disabled.clear();
    m_wrappers.clear();
  }

  std::set<String> m_disabled;
  std::map<String,std::unique_ptr<Wrapper>> m_wrappers;
};
} // empty namespace

typedef std::map<std::string,Wrapper*> wrapper_map_t;

// Global registry for wrappers
static wrapper_map_t s_wrappers;

// Request local registry for user defined wrappers and disabled builtins
static IMPLEMENT_THREAD_LOCAL(RequestWrappers, s_request_wrappers);

bool registerWrapper(const std::string &scheme, Wrapper *wrapper) {
  assert(s_wrappers.find(scheme) == s_wrappers.end());
  s_wrappers[scheme] = wrapper;
  return true;
}

bool disableWrapper(CStrRef scheme) {
  String lscheme = StringUtil::ToLower(scheme);

  if (lscheme.same("file")) {
    // Zend quietly succeeds, but does nothing
    return true;
  }

  bool ret = false;

  // Unregister request-specific wrappers entirely
  if (s_request_wrappers->m_wrappers.find(lscheme) !=
      s_request_wrappers->m_wrappers.end()) {
    s_request_wrappers->m_wrappers.erase(lscheme);
    ret = true;
  }

  // Disable builtin wrapper if it exists
  if (s_wrappers.find(lscheme.data()) == s_wrappers.end()) {
    // No builtin to disable
    return ret;
  }

  if (s_request_wrappers->m_disabled.find(lscheme) !=
      s_request_wrappers->m_disabled.end()) {
    // Already disabled
    return ret;
  }

  // Disable it
  s_request_wrappers->m_disabled.insert(lscheme);
  return true;
}

bool restoreWrapper(CStrRef scheme) {
  String lscheme = StringUtil::ToLower(scheme);
  bool ret = false;

  // Unregister request-specific wrapper
  if (s_request_wrappers->m_wrappers.find(lscheme) !=
    s_request_wrappers->m_wrappers.end()) {
    s_request_wrappers->m_wrappers.erase(lscheme);
    ret = true;
  }

  // Un-disable builtin wrapper
  if (s_request_wrappers->m_disabled.find(lscheme) ==
      s_request_wrappers->m_disabled.end()) {
    // Not disabled
    return ret;
  }

  // Perform action un-disable
  s_request_wrappers->m_disabled.erase(lscheme);
  return true;
}

bool registerRequestWrapper(CStrRef scheme, std::unique_ptr<Wrapper> wrapper) {
  String lscheme = StringUtil::ToLower(scheme);

  // Global, non-disabled wrapper
  if ((s_wrappers.find(lscheme.data()) != s_wrappers.end()) &&
      (s_request_wrappers->m_disabled.find(lscheme) ==
       s_request_wrappers->m_disabled.end())) {
    return false;
  }

  // A wrapper has already been registered for that scheme
  if (s_request_wrappers->m_wrappers.find(lscheme) !=
      s_request_wrappers->m_wrappers.end()) {
    return false;
  }

  s_request_wrappers->m_wrappers[lscheme] = std::move(wrapper);
  return true;
}

Array enumWrappers() {
  Array ret = Array::Create();

  // Enum global wrappers which are not disabled
  for (auto it = s_wrappers.begin(); it != s_wrappers.end(); ++it) {
    if (s_request_wrappers->m_disabled.find(it->first) ==
        s_request_wrappers->m_disabled.end()) {
      ret.append(it->first);
    }
  }

  // Enum request local wrappers
  for (auto it = s_request_wrappers->m_wrappers.begin();
       it != s_request_wrappers->m_wrappers.end(); ++it) {
    ret.append(it->first);
  }
  return ret;
}

Wrapper* getWrapper(CStrRef scheme) {
  String lscheme = StringUtil::ToLower(scheme);

  // Request local wrapper?
  {
    auto it = s_request_wrappers->m_wrappers.find(lscheme);
    if (it != s_request_wrappers->m_wrappers.end()) {
      return it->second.get();
    }
  }

  // Global, non-disabled wrapper?
  {
    auto it = s_wrappers.find(lscheme.data());
    if ((it != s_wrappers.end()) &&
        (s_request_wrappers->m_disabled.find(lscheme) ==
         s_request_wrappers->m_disabled.end())) {
      return it->second;
    }
  }

  return nullptr;
}

File* open(CStrRef uri, CStrRef mode, int options, CVarRef context) {
  const char *uri_string = uri.data();
  Wrapper *wrapper = nullptr;

  /* Special case for PHP4 Backward Compatability */
  if (!strncasecmp(uri_string, "zlib:", sizeof("zlib:") - 1)) {
    wrapper = getWrapper("compress.zlib");
  } else {
    const char *colon = strstr(uri_string, "://");
    if (colon) {
      wrapper = getWrapper(String(uri_string, colon - uri_string, CopyString));
    }
  }

  if (wrapper == nullptr) {
    wrapper = getWrapper("file");
  }
  assert(wrapper);

  return wrapper->open(uri, mode, options, context);
}

static FileStreamWrapper s_file_stream_wrapper;
static PhpStreamWrapper  s_php_stream_wrapper;
static HttpStreamWrapper s_http_stream_wrapper;

void RegisterCoreWrappers() {
  s_file_stream_wrapper.registerAs("file");
  s_php_stream_wrapper.registerAs("php");
  s_http_stream_wrapper.registerAs("http");
  s_http_stream_wrapper.registerAs("https");
}

///////////////////////////////////////////////////////////////////////////////
}}
