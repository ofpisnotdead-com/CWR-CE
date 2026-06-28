#ifdef _MSC_VER
#pragma once
#endif

#ifndef _XML_HPP
#define _XML_HPP

#include <Poseidon/Foundation/Strings/RString.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>

#include <Poseidon/IO/Streams/QStream.hpp>

// One attribute of an element: <TAG name=value>.
struct XMLAttribute
{
    RString name;
    RString value;
};

class XMLAttributes : public AutoArray<XMLAttribute>
{
  public:
    // Returns the index of the new attribute.
    int Add(RString name, RString value);
    const XMLAttribute* Find(RString name) const;
};

// SAX-style XML parser: derive, override the OnXxx callbacks, then call Parse.
// Abort stops parsing mid-stream. (See SquadParser.)
class SAXParser
{
  protected:
    bool _abort;

  public:
    SAXParser() { _abort = false; }

    // Parse the stream; the OnXxx callbacks fire during parsing. Returns false on error.
    bool Parse(QIStream& in);
    void Abort() { _abort = true; }

    virtual void OnStartDocument() {}
    virtual void OnEndDocument() {}
    virtual void OnStartElement(RString name, XMLAttributes& attributes)
    {
        (void)name;
        (void)attributes;
    }
    virtual void OnEndElement(RString name) { (void)name; }
    virtual void OnCharacters(RString chars) { (void)chars; }
};

bool DownloadFile(const char* url, const char* filename, const char* proxyServer, size_t maxSize = 0);

char* DownloadFile(const char* url, size_t& size, const char* proxyServer, size_t maxSize = 0);

#endif
