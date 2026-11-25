// BloodLib - packet field engine (original: PacketBuffer.cpp).
#include "PacketFormat.h"
#include "Log.h"

#include <string.h>
#include <wchar.h>

SOURCE_FILE("PacketBuffer.cpp");

namespace blood {
namespace {

void UnknownCompose(char type) {
    LogWrite(0, L"src\\PacketBuffer.cpp", 0xB1, L"Unknown type %C",
             static_cast<int>(static_cast<unsigned char>(type)));
    CRASH(0xB2, L"== CRASH == ASSERT CRASH");
}

void UnknownDecompose(char type) {
    LogWrite(0, L"src\\PacketBuffer.cpp", 0x193, L"Unknown type %C",
             static_cast<int>(static_cast<unsigned char>(type)));
}

void ComposeOverflow() {
    LogWrite(0, L"src\\PacketBuffer.cpp", 0xBC, L"Compose overflow.");
    CRASH(0xBD, L"== CRASH == ASSERT CRASH");
}

void DecomposeOverflow() {
    LogWrite(0, L"src\\PacketBuffer.cpp", 0x19D, L"Decompose overflow.");
}

u16 EngineCount(const char* format, u64 count) {
    if (count == 0 || count == static_cast<u64>(-1)) return static_cast<u16>(strlen(format));
    return static_cast<u16>(count);
}

} // namespace

u16 Packet_ComposeFields(u8* destination, u32 capacity, const char* format,
                         u64 fieldCount, va_list* arguments) {
    if (!destination || !format || !arguments) return 0;
    u8* write = destination;
    u8* end = destination + capacity;
    u8* start = destination;
    u64 count = EngineCount(format, fieldCount);
    for (u64 index = 0; index < count && format[index]; ++index) {
        char type = format[index];
        int width = 0;
        switch (type) {
        case 'c': case 'Q': width = 1; break;
        case 'h': case 'E': case 'U': case 'C': case 'u': width = 2; break;
        case 'i': case 'd': case 'f': width = 4; break;
        case 'q': width = 8; break;
        case 'o': width = 16; break;
        case ';': continue;
        case 's': case 'b': case 'K': case 'P': break;
        default: UnknownCompose(type); return 0;
        }
        if (width) {
            if (write + width > end) { ComposeOverflow(); return 0; }
            switch (type) {
            case 'c': case 'Q':
                *write = static_cast<u8>(va_arg(*arguments, int)); break;
            case 'h': case 'E': case 'U': case 'C': case 'u':
                *reinterpret_cast<u16*>(write) = static_cast<u16>(va_arg(*arguments, int)); break;
            case 'i': case 'd':
                *reinterpret_cast<u32*>(write) = static_cast<u32>(va_arg(*arguments, int)); break;
            case 'f': {
                float value = static_cast<float>(va_arg(*arguments, double));
                memcpy(write, &value, sizeof(value));
                break;
            }
            case 'q':
                *reinterpret_cast<u64*>(write) = va_arg(*arguments, u64); break;
            case 'o':
                memcpy(write, va_arg(*arguments, const void*), 16); break;
            }
            write += width;
        } else if (type == 's') {
            const wchar_t* text = va_arg(*arguments, const wchar_t*);
            size_t length = text ? wcslen(text) : 0;
            i16 signedLength = static_cast<i16>(static_cast<u16>(length));
            int bytes = static_cast<int>(signedLength) * 2;
            if (bytes < 0 || write + 2 + bytes > end) { ComposeOverflow(); return 0; }
            *reinterpret_cast<u16*>(write) = static_cast<u16>(signedLength);
            if (bytes) memcpy(write + 2, text, static_cast<size_t>(bytes));
            write += 2 + bytes;
        } else if (type == 'b' || type == 'K') {
            const void* data = va_arg(*arguments, const void*);
            i16 length = static_cast<i16>(static_cast<u16>(va_arg(*arguments, int)));
            if (length < 0 || write + 2 + length > end) { ComposeOverflow(); return 0; }
            *reinterpret_cast<u16*>(write) = static_cast<u16>(length);
            if (length) memcpy(write + 2, data, static_cast<size_t>(length));
            write += 2 + length;
        } else { // P
            u8 propertyType = static_cast<u8>(va_arg(*arguments, int));
            u16 id = static_cast<u16>(va_arg(*arguments, int));
            const void* data = va_arg(*arguments, const void*);
            i16 length = static_cast<i16>(static_cast<u16>(va_arg(*arguments, int)));
            if (write + 1 > end) { ComposeOverflow(); return 0; }
            *write++ = propertyType;
            if (write + 2 > end) { ComposeOverflow(); return 0; }
            *reinterpret_cast<u16*>(write) = id; write += 2;
            if (length < 0 || write + 2 + length > end) { ComposeOverflow(); return 0; }
            *reinterpret_cast<u16*>(write) = static_cast<u16>(length); write += 2;
            if (length) memcpy(write, data, static_cast<size_t>(length));
            write += length;
        }
    }
    return static_cast<u16>(write - start);
}

u16 Packet_DecomposeFields(const u8* source, u32 available, const char* format,
                           u64 fieldCount, va_list* arguments) {
    if (!source || !format || !arguments) return 0;
    const u8* read = source;
    const u8* end = source + available;
    const u8* start = source;
    u64 count = EngineCount(format, fieldCount);
    for (u64 index = 0; index < count && format[index]; ++index) {
        char type = format[index];
        int width = 0;
        switch (type) {
        case 'c': case 'Q': width = 1; break;
        case 'h': case 'E': case 'U': case 'C': case 'u': width = 2; break;
        case 'i': case 'd': case 'f': width = 4; break;
        case 'q': width = 8; break;
        case 'o': width = 16; break;
        case ';': continue;
        case 's': case 'b': case 'K': case 'P': break;
        default: UnknownDecompose(type); return 0;
        }
        if (width) {
            if (read + width > end) { DecomposeOverflow(); return 0; }
            memcpy(va_arg(*arguments, void*), read, static_cast<size_t>(width));
            read += width;
        } else if (type == 's') {
            wchar_t* output = va_arg(*arguments, wchar_t*);
            i16 capacity = static_cast<i16>(static_cast<u16>(va_arg(*arguments, int)));
            if (read + 2 > end) { DecomposeOverflow(); return 0; }
            i16 length = *reinterpret_cast<const i16*>(read);
            if (output) *output = L'\0';
            if (length < 0) {
                LogWrite(0, L"src\\PacketBuffer.cpp", 0x10C, L"length underflow"); return 0;
            }
            if (capacity < length + 1) {
                LogWrite(0, L"src\\PacketBuffer.cpp", 0x111, L"string buffer is too small"); return 0;
            }
            if (read + 2 + length * 2 > end) {
                LogWrite(0, L"src\\PacketBuffer.cpp", 0x116, L"length overflow"); return 0;
            }
            if (length) memcpy(output, read + 2, static_cast<size_t>(length * 2));
            output[length] = L'\0';
            read += 2 + length * 2;
        } else if (type == 'b' || type == 'K') {
            u8* output = va_arg(*arguments, u8*);
            i16* capacity = va_arg(*arguments, i16*);
            if (read + 2 > end) { DecomposeOverflow(); return 0; }
            i16 length = *reinterpret_cast<const i16*>(read);
            if (length < 0) {
                LogWrite(0, L"src\\PacketBuffer.cpp", 0x12C, L"src size underflow"); return 0;
            }
            if (*capacity < length) {
                LogWrite(0, L"src\\PacketBuffer.cpp", 0x131, L"not enough buffer"); return 0;
            }
            if (read + 2 + length > end) {
                LogWrite(0, L"src\\PacketBuffer.cpp", 0x136, L"buffer overflow"); return 0;
            }
            if (length) memcpy(output, read + 2, static_cast<size_t>(length));
            *capacity = length;
            read += 2 + length;
        } else { // P
            u8* typeOutput = va_arg(*arguments, u8*);
            u16* idOutput = va_arg(*arguments, u16*);
            u8* output = va_arg(*arguments, u8*);
            i16* capacity = va_arg(*arguments, i16*);
            if (read + 5 > end) { DecomposeOverflow(); return 0; }
            u8 propertyType = read[0];
            i16 id = *reinterpret_cast<const i16*>(read + 1);
            i16 length = *reinterpret_cast<const i16*>(read + 3);
            if (propertyType <= 0) {
                LogWrite(0, L"src\\PacketBuffer.cpp", 0x150, L"invalid property type id"); return 0;
            }
            if (id < 0) {
                LogWrite(0, L"src\\PacketBuffer.cpp", 0x155, L"src count underflow"); return 0;
            }
            if (length < 0) {
                LogWrite(0, L"src\\PacketBuffer.cpp", 0x15A, L"src size underflow"); return 0;
            }
            if (*capacity < length) {
                LogWrite(0, L"src\\PacketBuffer.cpp", 0x15F, L"not enough buffer"); return 0;
            }
            if (read + 5 + length + 4 > end) {
                LogWrite(0, L"src\\PacketBuffer.cpp", 0x164, L"buffer overflow"); return 0;
            }
            if (length) memcpy(output, read + 5, static_cast<size_t>(length));
            *typeOutput = propertyType;
            *capacity = length;
            *idOutput = static_cast<u16>(id);
            read += 5 + length;
        }
    }
    return static_cast<u16>(read - start);
}

u16 ComposeFormat(void* destination, int capacity, const char* format, int fieldCount, ...) {
    va_list ap;
    va_start(ap, fieldCount);
    u16 result = Packet_ComposeFields(static_cast<u8*>(destination), static_cast<u32>(capacity),
                                      format, fieldCount < 0 ? static_cast<u64>(-1) : fieldCount, &ap);
    va_end(ap);
    return result;
}

u16 ComposeFormatV(void* destination, int capacity, const char* format, int fieldCount,
                   va_list arguments) {
    return Packet_ComposeFields(static_cast<u8*>(destination), static_cast<u32>(capacity),
                                format, fieldCount < 0 ? static_cast<u64>(-1) : fieldCount,
                                &arguments);
}

u16 DecomposeFormat(const void* source, int available, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    u16 result = Packet_DecomposeFields(static_cast<const u8*>(source),
                                        static_cast<u32>(available), format,
                                        static_cast<u64>(-1), &ap);
    va_end(ap);
    return result;
}

u16 DecomposeFormatV(const void* source, int available, const char* format,
                     va_list arguments) {
    return Packet_DecomposeFields(static_cast<const u8*>(source),
                                  static_cast<u32>(available), format,
                                  static_cast<u64>(-1), &arguments);
}

} // namespace blood
