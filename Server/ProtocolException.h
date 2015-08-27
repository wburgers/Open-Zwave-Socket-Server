// ProtocolException class

#ifndef OZWSS_PROTOCOLEXCEPTION_H_
#define OZWSS_PROTOCOLEXCEPTION_H_

#include <string>
#include <sstream>

class ProtocolException
{
 public:
  ProtocolException (int code, std::string s ) : m_code(code), m_s ( s ) {};
  ~ProtocolException (){};

  std::string what() {
	std::stringstream code;
	code << m_code;
	std::string what = "";
	what += code.str();
	what += ": ";
	what += m_s;
	return what;
  }

 private:
  int m_code;
  std::string m_s;

};

#endif // OZWSS_PROTOCOLEXCEPTION_H_