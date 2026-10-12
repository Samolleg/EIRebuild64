#pragma once

// -------------------------------------------------------------------------------------------------
// Report an error or warning
// -------------------------------------------------------------------------------------------------

// Report error
bool ReportError(const char *format, ...);
bool ReportError(HRESULT code, const char *format, ...);
bool ReportError(HRESULT code);
bool ReportRecoverableError(const char *format, ...);

// Native 0x44C470 debug channel; ordinary notifications keep their own sinks.
void ReportGameDebug(const char *format, ...);

// Report warning
void ReportWarning(const char *format, ...);
void ReportWarning(HRESULT code, const char *format, ...);
void ReportWarning(HRESULT code);

const char *AnimationFlagsToString(DWORD flags);

// -------------------------------------------------------------------------------------------------
// CGameException
// -------------------------------------------------------------------------------------------------

class CGameException
{
protected:
    string m_errorString;

public:
    CGameException() = default;
    CGameException(const char *format, ...);
    virtual ~CGameException() = default;

    const char *GetErrorString() const { return m_errorString.c_str(); }
};

// -------------------------------------------------------------------------------------------------
// CDXException
// -------------------------------------------------------------------------------------------------

class CDXException : public CGameException
{
public:
    CDXException(const char *format, ...);
    CDXException(HRESULT code, const char *format, ...);
};
