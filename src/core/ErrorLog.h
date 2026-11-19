// Port of ErrorHandler: a message + accumulated text shown in the error window.
#pragma once
#include <string>

namespace ae {

class ErrorLog {
  public:
    static ErrorLog& instance() {
        static ErrorLog e;
        return e;
    }
    void addText(const std::string& t) { content += t + "\n"; }
    void setMessage(const std::string& m) { message = m; }
    void clearMessages() { content.clear(); }
    void showWindow() { visible = true; }

    std::string message;
    std::string content;
    bool visible = false;
};

}  // namespace ae
