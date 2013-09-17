// ProtocolException class

#ifndef ProtocolException_class
#define ProtocolException_class

#include <string>

class ProtocolException
{
 public:
  ProtocolException (int code, std::string s ) : m_code(code), m_s ( s ) {};
  ~ProtocolException (){};

  std::string what() {
	std::string what = "";
	what += m_code;
	what += ": ";
	what += m_s;
	return what;
  }

 private:
  int m_code;
  std::string m_s;

};

#endif
