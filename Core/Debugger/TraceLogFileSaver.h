#pragma once
#include "pch.h"

class TraceLogFileSaver
{
private:
	bool _enabled = false;
	string _outputFilepath;
	string _outputBuffer;
	ofstream _outputFile;

public:
	~TraceLogFileSaver()
	{
		StopLogging();
	}

	//Returns false when the file could not be opened (invalid path, unwritable directory, ...)
	//instead of silently "logging" into a dead stream.
	bool StartLogging(string filename)
	{
		if(_enabled) {
			//Already logging to another file - close it first (also flushes its buffer)
			StopLogging();
		}

		_outputBuffer.clear();
		_outputFile.clear();
		_outputFile.open(filename, ios::out | ios::binary);
		if(!_outputFile.is_open()) {
			_enabled = false;
			return false;
		}

		_enabled = true;
		return true;
	}

	void StopLogging()
	{
		if(_enabled) {
			_enabled = false;
			if(_outputFile) {
				if(!_outputBuffer.empty()) {
					_outputFile << _outputBuffer;
				}
				_outputFile.close();
			}
		}
	}

	__forceinline bool IsEnabled() { return _enabled; }

	void Log(string& log)
	{
		_outputBuffer += log + '\n';
		// Flush incrementally so the trace tail reaches the OS and survives a crash.
		// A large buffer would lose the rows closest to the crash (the ones we need most).
		if(_outputBuffer.size() > 1024) {
			_outputFile << _outputBuffer;
			_outputFile.flush();
			_outputBuffer.clear();
		}
	}
};