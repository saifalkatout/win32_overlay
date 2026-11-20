#pragma once

#include <windows.h>

struct HrError {
	HRESULT hr;
	const char* message;
};
		
void inline ThrowIfFailed(HRESULT hr, const char* what) {
	if (FAILED(hr)) throw HrError{ hr, what };
}