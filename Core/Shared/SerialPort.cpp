#include "Shared/SerialPort.h"
#include "Shared/MessageManager.h"

#ifdef _WIN32

// We cannot include <windows.h> because the Core project uses /Za (no MS extensions)
// which conflicts with anonymous structs in the Windows SDK headers.
// Instead, we declare the Win32 API functions as extern "C" and define minimal
// structure layouts compatible with the Windows ABI. On x64, all calling conventions
// are the same, so we don't need __stdcall. kernel32.lib is always linked by default.

extern "C" {
	void* CreateFileA(const char* lpFileName, unsigned long dwDesiredAccess, unsigned long dwShareMode, void* lpSecurityAttributes, unsigned long dwCreationDisposition, unsigned long dwFlagsAndAttributes, void* hTemplateFile);
	int   CloseHandle(void* hObject);
	int   WriteFile(void* hFile, const void* lpBuffer, unsigned long nNumberOfBytesToWrite, unsigned long* lpNumberOfBytesWritten, void* lpOverlapped);
	int   ReadFile(void* hFile, void* lpBuffer, unsigned long nNumberOfBytesToRead, unsigned long* lpNumberOfBytesRead, void* lpOverlapped);
	int   GetCommState(void* hFile, void* lpDCB);
	int   SetCommState(void* hFile, const void* lpDCB);
	int   SetCommTimeouts(void* hFile, const void* lpCommTimeouts);
	int   SetupComm(void* hFile, unsigned long dwInQueue, unsigned long dwOutQueue);
	unsigned long GetLastError();
}

#pragma comment(lib, "kernel32.lib")

static const long long INVALID_HANDLE_VALUE_LL = -1;

// Constants
static constexpr unsigned long _GENERIC_READ  = 0x80000000;
static constexpr unsigned long _GENERIC_WRITE = 0x40000000;
static constexpr unsigned long _OPEN_EXISTING = 3;
static constexpr unsigned long _FILE_ATTRIBUTE_NORMAL = 0x80;
static constexpr unsigned long _FILE_SHARE_READ  = 1;
static constexpr unsigned long _FILE_SHARE_WRITE = 2;
static constexpr unsigned long _MAXDWORD = 0xFFFFFFFF;

// DCB structure — layout-compatible with the Windows DCB definition
struct WinDCB {
	unsigned long DCBlength;
	unsigned long BaudRate;
	unsigned long fBinary : 1;
	unsigned long fParity : 1;
	unsigned long fOutxCtsFlow : 1;
	unsigned long fOutxDsrFlow : 1;
	unsigned long fDtrControl : 2;
	unsigned long fDsrSensitivity : 1;
	unsigned long fTXContinueOnXoff : 1;
	unsigned long fOutX : 1;
	unsigned long fInX : 1;
	unsigned long fErrorChar : 1;
	unsigned long fNull : 1;
	unsigned long fRtsControl : 2;
	unsigned long fAbortOnError : 1;
	unsigned long fDummy2 : 17;
	unsigned short wReserved;
	unsigned short XonLim;
	unsigned short XoffLim;
	unsigned char ByteSize;
	unsigned char Parity;
	unsigned char StopBits;
	char XonChar;
	char XoffChar;
	char ErrorChar;
	char EofChar;
	char EvtChar;
	unsigned short wReserved1;
};

// COMMTIMEOUTS structure — layout-compatible with the Windows definition
struct WinCOMMTIMEOUTS {
	unsigned long ReadIntervalTimeout;
	unsigned long ReadTotalTimeoutMultiplier;
	unsigned long ReadTotalTimeoutConstant;
	unsigned long WriteTotalTimeoutMultiplier;
	unsigned long WriteTotalTimeoutConstant;
};

SerialPort::SerialPort() : _handle((void*)INVALID_HANDLE_VALUE_LL) {}
SerialPort::~SerialPort() { Close(); }

bool SerialPort::IsOpen() const
{
	return _handle != (void*)INVALID_HANDLE_VALUE_LL;
}

bool SerialPort::Open(const string& portName, uint32_t baudRate)
{
	Close();
	if(portName.empty()) {
		MessageManager::Log("[SerialPort] Open failed: empty port name");
		return false;
	}

	// Build full device path for CreateFile (supports COM1-COM256)
	string path = portName;
	if(path.find("\\\\.\\") != 0) {
		path = "\\\\.\\" + path;
	}

	MessageManager::Log("[SerialPort] Attempting to open: " + path);

	// Retry loop — on ROM reload, the previous bridge may still be closing the port
	void* h = nullptr;
	for(int attempt = 0; attempt < 10; attempt++) {
		h = CreateFileA(
			path.c_str(),
			_GENERIC_READ | _GENERIC_WRITE,
			_FILE_SHARE_READ | _FILE_SHARE_WRITE,
			nullptr,
			_OPEN_EXISTING,
			_FILE_ATTRIBUTE_NORMAL,
			nullptr
		);
		if(h != (void*)INVALID_HANDLE_VALUE_LL) break;

		unsigned long err = GetLastError();
		if(err != 5) {
			// Error 5 = access denied (port still closing), retry
			// Any other error: give up immediately
			MessageManager::Log("[SerialPort] CreateFileA failed for " + path + " (GetLastError=" + std::to_string(err) + ")");
			return false;
		}
		// Port still held — wait and retry
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	if(h == (void*)INVALID_HANDLE_VALUE_LL) {
		MessageManager::Log("[SerialPort] CreateFileA failed for " + path + " after 10 retries (GetLastError=5)");
		return false;
	}
	_handle = h;
	MessageManager::Log("[SerialPort] CreateFileA succeeded, configuring port...");

	// Configure port
	WinDCB dcb = {};
	dcb.DCBlength = sizeof(WinDCB);
	if(!GetCommState(h, &dcb)) {
		MessageManager::Log("[SerialPort] GetCommState failed");
		Close();
		return false;
	}

	dcb.BaudRate = baudRate;
	dcb.ByteSize = 8;
	dcb.Parity = 0;     // NOPARITY
	dcb.StopBits = 0;   // ONESTOPBIT
	dcb.fBinary = 1;
	dcb.fParity = 0;
	dcb.fOutxCtsFlow = 0;
	dcb.fOutxDsrFlow = 0;
	dcb.fDtrControl = 1; // DTR_CONTROL_ENABLE
	dcb.fDsrSensitivity = 0;
	dcb.fTXContinueOnXoff = 0;
	dcb.fOutX = 0;
	dcb.fInX = 0;
	dcb.fErrorChar = 0;
	dcb.fNull = 0;
	dcb.fRtsControl = 1; // RTS_CONTROL_ENABLE
	dcb.fAbortOnError = 0;

	if(!SetCommState(h, &dcb)) {
		MessageManager::Log("[SerialPort] SetCommState failed");
		Close();
		return false;
	}

	// Set timeouts: non-blocking reads (return immediately with whatever is available)
	WinCOMMTIMEOUTS timeouts = {};
	timeouts.ReadIntervalTimeout = _MAXDWORD;         // Return immediately between bytes
	timeouts.ReadTotalTimeoutMultiplier = 0;
	timeouts.ReadTotalTimeoutConstant = 0;             // No overall timeout — instant return
	timeouts.WriteTotalTimeoutMultiplier = 0;
	timeouts.WriteTotalTimeoutConstant = 0;
	SetCommTimeouts(h, &timeouts);

	SetupComm(h, 4096, 4096);
	MessageManager::Log("[SerialPort] Port opened successfully: " + portName);
	return true;
}

void SerialPort::Close()
{
	if(_handle != (void*)INVALID_HANDLE_VALUE_LL) {
		CloseHandle(_handle);
		_handle = (void*)INVALID_HANDLE_VALUE_LL;
	}
}

int SerialPort::Write(const uint8_t* data, int len)
{
	if(!IsOpen() || len <= 0) return 0;
	unsigned long written = 0;
	WriteFile(_handle, data, (unsigned long)len, &written, nullptr);
	return (int)written;
}

int SerialPort::Read(uint8_t* buffer, int maxLen, uint32_t /*timeoutMs*/)
{
	// Timeout is handled by COMMTIMEOUTS set in Open() (~100ms)
	if(!IsOpen() || maxLen <= 0) return 0;
	unsigned long bytesRead = 0;
	ReadFile(_handle, buffer, (unsigned long)maxLen, &bytesRead, nullptr);
	return (int)bytesRead;
}

#else // Linux / macOS

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>

static int FdFromHandle(void* h) { return (int)(intptr_t)h; }
static void* HandleFromFd(int fd) { return (void*)(intptr_t)fd; }

SerialPort::SerialPort() : _handle(HandleFromFd(-1)) {}
SerialPort::~SerialPort() { Close(); }

bool SerialPort::IsOpen() const
{
	return FdFromHandle(_handle) >= 0;
}

bool SerialPort::Open(const string& portName, uint32_t baudRate)
{
	Close();
	if(portName.empty()) return false;

	int fd = open(portName.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
	if(fd < 0) return false;

	int flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

	struct termios tty = {};
	if(tcgetattr(fd, &tty) != 0) {
		close(fd);
		return false;
	}

	speed_t speed;
	switch(baudRate) {
		case 9600:   speed = B9600;   break;
		case 19200:  speed = B19200;  break;
		case 38400:  speed = B38400;  break;
		case 57600:  speed = B57600;  break;
		case 115200: speed = B115200; break;
#ifdef B230400
		case 230400: speed = B230400; break;
#endif
#ifdef B460800
		case 460800: speed = B460800; break;
#endif
		default:     speed = B9600;   break;
	}
	cfsetispeed(&tty, speed);
	cfsetospeed(&tty, speed);

	tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
	tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
	tty.c_cflag |= CREAD | CLOCAL;
	tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
#ifdef IEXTEN
	tty.c_lflag &= ~IEXTEN;
#endif
	tty.c_iflag &= ~(IXON | IXOFF | IXANY | INLCR | ICRNL | IGNCR | BRKINT | PARMRK | ISTRIP);
#ifdef IGNPAR
	tty.c_iflag &= ~IGNPAR;
#endif
	tty.c_oflag &= ~OPOST;
	tty.c_cc[VMIN] = 0;
	tty.c_cc[VTIME] = 1;

	if(tcsetattr(fd, TCSANOW, &tty) != 0) {
		close(fd);
		return false;
	}

	tcflush(fd, TCIOFLUSH);
	_handle = HandleFromFd(fd);
	return true;
}

void SerialPort::Close()
{
	int fd = FdFromHandle(_handle);
	if(fd >= 0) {
		close(fd);
		_handle = HandleFromFd(-1);
	}
}

int SerialPort::Write(const uint8_t* data, int len)
{
	if(!IsOpen() || len <= 0) return 0;
	int written = (int)write(FdFromHandle(_handle), data, (size_t)len);
	return written > 0 ? written : 0;
}

int SerialPort::Read(uint8_t* buffer, int maxLen, uint32_t timeoutMs)
{
	if(!IsOpen() || maxLen <= 0) return 0;

	int fd = FdFromHandle(_handle);
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);

	struct timeval tv;
	tv.tv_sec = timeoutMs / 1000;
	tv.tv_usec = (timeoutMs % 1000) * 1000;

	int ret = select(fd + 1, &readfds, nullptr, nullptr, &tv);
	if(ret > 0 && FD_ISSET(fd, &readfds)) {
		int n = (int)read(fd, buffer, (size_t)maxLen);
		return n > 0 ? n : 0;
	}
	return 0;
}

#endif