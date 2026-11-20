// winprops: what Windows itself reads from a file (the Details tab in
// Explorer's Properties), and writing through Windows, to see what it puts
// where. Not shipped.
//
//   winprops get <file>
//   winprops set <file> Title=... Comment=... Rating=75 ...

#include <windows.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shobjidl.h>

#include <cstdio>
#include <string>
#include <vector>

struct Key {
    const wchar_t* name;
    PROPERTYKEY key;
    enum { Text, List, Number } kind;
};

static const std::vector<Key>& keys()
{
    static const std::vector<Key> k{
        {L"Title", PKEY_Title, Key::Text},
        {L"Subtitle", PKEY_Media_SubTitle, Key::Text},
        {L"Subject", PKEY_Subject, Key::Text},
        {L"Author", PKEY_Author, Key::List},
        {L"Artist", PKEY_Music_Artist, Key::List},
        {L"AlbumArtist", PKEY_Music_AlbumArtist, Key::Text},
        {L"Album", PKEY_Music_AlbumTitle, Key::Text},
        {L"Genre", PKEY_Music_Genre, Key::List},
        {L"Year", PKEY_Media_Year, Key::Number},
        {L"Track", PKEY_Music_TrackNumber, Key::Number},
        {L"Composer", PKEY_Music_Composer, Key::List},
        {L"Publisher", PKEY_Media_Publisher, Key::Text},
        {L"Copyright", PKEY_Copyright, Key::Text},
        {L"Comment", PKEY_Comment, Key::Text},
        {L"Keywords", PKEY_Keywords, Key::List},
        {L"Rating", PKEY_Rating, Key::Number},
    };
    return k;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) {
        std::wprintf(L"usage: winprops get|set <file> [Name=value ...]\n");
        return 2;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool set = std::wstring(argv[1]) == L"set";
    IPropertyStore* store = nullptr;
    HRESULT hr = SHGetPropertyStoreFromParsingName(argv[2], nullptr, set ? GPS_READWRITE : GPS_DEFAULT, IID_PPV_ARGS(&store));
    if (FAILED(hr)) {
        std::wprintf(L"Windows can't open its properties (0x%08lx)\n", hr);
        return 1;
    }
    if (!set) {
        for (const Key& k : keys()) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(store->GetValue(k.key, &v)) && v.vt != VT_EMPTY) {
                PWSTR s = nullptr;
                if (SUCCEEDED(PropVariantToStringAlloc(v, &s))) {
                    std::wprintf(L"%ls = %ls\n", k.name, s);
                    CoTaskMemFree(s);
                }
            }
            PropVariantClear(&v);
        }
    } else {
        for (int i = 3; i < argc; ++i) {
            const std::wstring a = argv[i];
            const size_t eq = a.find(L'=');
            const std::wstring name = a.substr(0, eq), value = a.substr(eq + 1);
            for (const Key& k : keys()) {
                if (name != k.name) continue;
                PROPVARIANT v;
                if (k.kind == Key::Number) InitPropVariantFromUInt32(std::wcstoul(value.c_str(), nullptr, 10), &v);
                else if (k.kind == Key::List) InitPropVariantFromStringAsVector(value.c_str(), &v);
                else InitPropVariantFromString(value.c_str(), &v);
                hr = store->SetValue(k.key, v);
                std::wprintf(L"%ls: %ls\n", k.name, SUCCEEDED(hr) ? L"set" : L"Windows refused");
                PropVariantClear(&v);
            }
        }
        hr = store->Commit();
        std::wprintf(L"commit: %ls (0x%08lx)\n", SUCCEEDED(hr) ? L"ok" : L"failed", hr);
    }
    store->Release();
    CoUninitialize();
    return 0;
}
