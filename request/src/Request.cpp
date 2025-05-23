#include "../inc/Request.hpp"
#include "../../common/Config.hpp"

Request::Request(PORT port) : _port(port), _state(HEADERS), _contentLength(0), _maxRequestSize(10 * 1024 * 1024)  {
	_buffer.reserve(1024);
	Server tmp = Config::getServer(port);
	if (tmp.getName() == "") {
		LOG_ERROR("Request::Request: Server not found.");
		throw NotFound_404;
	}
	_maxBodySize = Config::getServer(port).getBodySize();
}

Request::Request(const Request& other) : _state(other._state), _contentLength(other._contentLength), _maxRequestSize(other._maxRequestSize), _maxBodySize(other._maxBodySize) {
	_headers = other._headers;
	_buffer = other._buffer;
	_body = other._body;
}

Request& Request::operator=(const Request& rhs) {
	if (this != &rhs) {
		_headers = rhs._headers;
		_buffer = rhs._buffer;
		_body = rhs._body;
		_state = rhs._state;
		_contentLength = rhs._contentLength;
		_maxRequestSize = rhs._maxRequestSize;
	}
	return *this;
}

Request::~Request() {
	_contentLength = 0;
	_maxRequestSize = 0;
	_state = DONE;
	_headers.clear();
	_buffer.clear();
	_body.clear();
}
/**
 * @brief Request Header를 반환
 * @return Request Header
*/
RequestHeaders Request::getHeaders() const {
	return _headers;
}
/**
 * @brief 원하는 헤더의 값을 반환
 * @param key 헤더의 키 ex) Host, User-Agent ...
 * @return 헤더의 값
 * @note 헤더의 값이 없을 경우 빈 문자열 반환
*/
std::string Request::getHeader(const std::string& key) const {
	std::map<std::string, std::string>::const_iterator it = _headers.find(key);
	if (it != _headers.end()) {
		return it->second;
	}
	return "";
}
/**
 * @brief Request Body를 반환
 * @return Request Body
*/
String::BinaryBuffer Request::getBody() const {
	return _body;
}
/**
 * @brief 헤더를 추가
 * @param key 헤더의 키 ex) Host, User-Agent ...
 * @param value 헤더의 값
*/
void Request::setHeader(const std::string& key, const std::string& value) {
	_headers[key] = value;
}
/**
 * @brief 헤더를 삭제
 * @param key 삭제할 헤더의 키 ex) Host, User-Agent ...
*/
void Request::removeHeader(const std::string& key) {
	_headers.erase(key);
}
/**
 * @brief 모든 헤더를 삭제
*/
void Request::clearHeaders() {
	_headers.clear();
}
/**
 * @brief Request Data를 파싱
 * @param buffer Request Data
 * @note Request Data를 파싱하여 Request Data, Header, Body를 추출
*/
void Request::parseBufferedData(const BinaryBuffer& buffer) {
	LOG_DEBUG(buffer.str());
	LOG_DEBUG("Request::parseBufferedData: Parsing request data.");
	if (_buffer.size() + buffer.size() > _maxRequestSize) {
		LOG_ERROR("Request::parseBufferedData: Request size exceeds maximum size.");
		throw PayloadTooLarge_413;
		return;
	}

	_buffer += buffer;

	try {
		bool continueParsing = true;
		while (continueParsing && _state != DONE) {
			switch (_state)
			{
			case HEADERS:
				continueParsing = parseHeaders();
				LOG_DEBUG("Request::parseBufferedData: Request parsing complete.");
				break;
			case BODY:
				continueParsing = parseBody();
				break;
			case CHUNKED_BODY:
				continueParsing = parseChunkedBody();
				break;
			case DONE:
				LOG_DEBUG("Request::parseBufferedData: Request parsing complete.");
				_buffer.clear();
				continueParsing = false;
				break;
			}
		}
	} catch (std::exception& e) {
		LOG_DEBUG("Request::parseBufferedData: Request parsing complete.");
		_state = DONE;
	} catch (const Status &e) {
		LOG_DEBUG("Request::parseBufferedData: Request parsing complete.");
		std::cout << "Error parsing request: " << String::Itos(e) << std::endl;
		_state = DONE;
		throw e;
	}
	LOG_DEBUG("Request::parseBufferedData: Request parsing complete.");
}
/**
 * @brief 헤더 파싱
 * @return 헤더 파싱 완료 여부
*/
bool Request::parseHeaders() {
	while (_state == HEADERS && !_buffer.empty()) {
		BinaryBuffer line = _buffer.readLine();

		if (line.empty()) {
			return false;
		}

		if (line[line.size() - 2] != '\r') {
			LOG_ERROR("Request::parseHeaders: Invalid request header format.");
			throw BadRequest_400;
		}

		if (line == "\r\n") {
			finishHeaders();
			return true;
		}

		if (_headers.empty()) {
			parseRequestLine(line.str());
		} else {
			parseRequestHeader(line.str());
		}
	}
	return false;
}
/**
 * @brief 헤더 파싱 완료
*/
void Request::finishHeaders() {
	std::string contentLength = getHeader("Content-Length");
	std::string Method = getHeader("Method");
	if (!contentLength.empty() && Method == "POST") {
		try {
			_contentLength = String::ToUL(contentLength);
			_state = BODY;
		}
		catch (std::exception& e) {
			LOG_ERROR("Request::finishHeaders: Invalid content length.");
			throw BadRequest_400;
		}
	} else if (getHeader("Transfer-Encoding") == "chunked") {
		_state = CHUNKED_BODY;
	} else if (Method == "GET") {
		_state = DONE;
	} else {
		LOG_ERROR("Request::finishHeaders: Invalid request.");
		throw BadRequest_400;
	}
}
/**
 * @brief Request Line 파싱
 * @param line Request Line
*/
void Request::parseRequestLine(const std::string& line) {
	std::istringstream iss(line);
	std::string method, uri, version;
	if (!(iss >> method >> uri >> version) || iss.fail() ){
		LOG_ERROR("Request::parseRequestLine: Invalid request line format.");
		throw MisdirectedRequest_421;
	}

	if (method != "GET" && method != "POST" && method != "PUT" && method != "DELETE") {
		LOG_ERROR("Request::parseRequestLine: Invalid request method.");
		throw MethodNotAllowed_405;
	}

	if (version != "HTTP/1.1") {
		LOG_ERROR("Request::parseRequestLine: Invalid request version.");
		throw HttpVersionNotSupported_505;
	}

	if (uri.size() > 1024) {
		LOG_ERROR("Request::parseRequestLine: Request URI exceeds maximum size.");
		throw UriTooLong_414;
	}

	int allowMethod = Config::getServer(_port).getLocation(uri).getMethods();
	if ((method == "GET" && !(allowMethod & (1 << GET))) ||
		(method == "POST" && !(allowMethod & (1 << POST))) ||
		(method == "PUT" && !(allowMethod & (1 << PUT))) ||
		(method == "DELETE" && !(allowMethod & (1 << DELETE)))) {
		LOG_ERROR("Request::parseRequestLine: Method not allowed.");
		throw MethodNotAllowed_405;
	}
	setHeader("Method", method);
	setHeader("URI", uri);
	setHeader("Version", version);
}
/**
 * @brief Request Header 파싱
 * @param line Request Header
 * @note Request Header를 파싱하여 헤더를 추가
 * @note Request Header의 형식이 잘못되었을 경우 BadRequest_400 예외 발생
*/
void Request::parseRequestHeader(const std::string& line) {
	size_t pos = line.find(":");
	if (pos == std::string::npos) {
		LOG_ERROR("Request::parseRequestHeader: Invalid request header format.");
		throw BadRequest_400;
	}

	std::string key = line.substr(0, pos);
	std::string value = line.substr(pos + 1);

	key = String::Trim(key);
	value = String::Trim(value);

	if (key.empty() || value.empty()) {
		LOG_ERROR("Request::parseRequestHeader: Invalid request header format.");
		throw BadRequest_400;
	}

	if (value.size() > 1024) {
		LOG_ERROR("Request::parseRequestHeader: Request header value exceeds maximum size.");
		throw RequestHeaderFieldsTooLarge_431;
	}

	setHeader(key, value);
}
/**
 * @brief Request Body 파싱
 * @return Request Body 파싱 완료 여부
 * @note Content-Length 헤더가 있을 경우 Request Body를 파싱
 * @note Request Body의 크기가 최대 크기를 초과할 경우 PayloadTooLarge_413 예외 발생
*/
bool Request::parseBody() {
	if (_contentLength > _maxBodySize) {

		LOG_ERROR("Request::parseBody: Request body size exceeds maximum size.");
		throw PayloadTooLarge_413;
	}
	if (_buffer.size() >= _contentLength) {
		_body += _buffer.subStr(0, _contentLength);
		_buffer.remove(0, _contentLength);
		if (_buffer.size() == 2 || _buffer.empty()) {
			_buffer.clear();
			_state = DONE;
		} else {
			LOG_ERROR("Request::parseBody: Invalid request body format.");
			throw BadRequest_400;
		}
	}
	return false;
}
/**
 * @brief Chunked Body 파싱
 * @return Chunked Body 파싱 완료 여부
 * @note Chunked Body를 파싱하여 Request Body를 추출
*/
bool Request::parseChunkedBody() {
	while (_state == CHUNKED_BODY && !_buffer.empty()) {
		if (_contentLength == 0) {
			BinaryBuffer line = _buffer.readLine();
			if (line.empty()) return false;

			_contentLength = std::strtol(line.c_str(), NULL, 16);
			if (_contentLength == 0) {
				_state = DONE;
				return false;
			}
		}

		if (_buffer.size() < _contentLength + 2) return false;

		_body += _buffer.subStr(0, _contentLength);
		_buffer.remove(0, _contentLength + 2);
		_contentLength = 0;
	}
	return false;
}

bool Request::isDone() const {
	return _state == DONE;
}

void Request::clear() {
	_contentLength = 0;
	_state = HEADERS;
	_headers.clear();
	_buffer.clear();
	_body.clear();
}

PORT Request::getPort() const {
	return _port;
}