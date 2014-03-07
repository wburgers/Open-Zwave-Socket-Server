// ProtocolException class

#ifndef ProtocolException_class
#define ProtocolException_class

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

#endif
