#ifndef SERVERSOCKET_HPP
#define SERVERSOCKET_HPP

#include "../../common/ErrorLog.hpp"
#include "../../common/Define.hpp"
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stdexcept>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>


class ServerSocket {

public:
	ServerSocket(const std::string &host, const std::string &port);
	~ServerSocket();
	ServerSocket(const ServerSocket &rhs);
	ServerSocket &operator=(const ServerSocket &rhs);

	FD	accept();

	void setSockOpt(int level, int optname, int opt);
	void setAutoSockOpt();
	void setNonBlocking(FD socket);
	void initServerSocket();

	void logError(const std::string &msg) const;

	FD getServerSocket() const;
	const char *getHost() const;
	const char *getPort() const;
	std::string getServerIP() const;

	class SocketException : public std::exception {
	public:
		SocketException(const std::string &msg) : _msg(msg) {}
		virtual const char *what() const throw();
		virtual ~SocketException() throw() {}
	private:
		std::string _msg;
	};
	
private:
	static const int DEFAULTBACKLOG = 10;
	static const int DEFAULTBUFFER = 1024;
	
	void	socket();
	void	bind();
	void	listen(int backlog);
	void	close();

	ServerSocket();
	
	const char *_host;
	const char *_port;
	int _serverSocket;
	std::string _serverIP;
};

#endif