#ifdef _MSC_VER
    #define _CRT_SECURE_NO_WARNINGS
#endif

#include <cassert>
#include <cstdint>
#include <cstring>
#include <optional>
#include <regex>
#include <vector>

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Demangle/Demangle.h"

using namespace llvm;

class BinaryReader {
private:
    const std::vector<std::uint8_t>&    data;
    size_t                              cur;
    bool                                is_32bit;
public:
    BinaryReader(const std::vector<std::uint8_t>& data) : data(data), cur(0), is_32bit(false) { }

    void set_is_32(bool is_32bit) { this->is_32bit = is_32bit; }

    void seek(size_t off)       { cur = off; }

    std::uint8_t read_u8()      { return read_core_type<std::uint8_t>(); }
    std::uint16_t read_u16()    { return read_core_type<std::uint16_t>(); }
    std::uint32_t read_u32()    { return read_core_type<std::uint32_t>(); }
    std::uint64_t read_u64()    { return read_core_type<std::uint64_t>(); }

    std::uint64_t read_ptr() { return (is_32bit) ? read_u32() : read_u64(); }

    void read_bytes(uint8_t* buf, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            buf[i] = read_u8();
        }
    }

private:
    template <typename T>
    T read_core_type() {
        if (cur + sizeof(T) > data.size()) {
            return T();
        }
        // TODO endian swap
        T result = *reinterpret_cast<const T*>(&data[cur]);
        cur += sizeof(T);
        return result;
    }
};

static void print_usage_and_die() {
    std::fprintf(stderr, "USAGE: vtabledump <file> [--mangled] [--json] [--filter=<regex>]\n");
    std::exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
    // Parse args
    if (argc < 2) { print_usage_and_die(); }
    const char*                 arg_file = argv[1];
    bool                        arg_json = false;
    bool                        arg_mangled = false;
    std::optional<std::regex>   arg_filter = { };
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--json") == 0) {
            // --json
            arg_json = true;
        } else if (std::strcmp(argv[i], "--mangled") == 0) {
            // --mangled
            arg_mangled = true;
        } else if (std::strncmp(argv[i], "--filter=", std::strlen("--filter=")) == 0) {
            // --filter=<regex>
            const char* filter_part = argv[i] + std::strlen("--filter") + 1;
            try {
                arg_filter = std::regex(filter_part);
            } catch (const std::regex_error& err) {
                std::fprintf(stderr, "Error: Filter is not valid regex: %s\n", err.what());
                return EXIT_FAILURE;
            }
        } else {
            std::fprintf(stderr, "Error: Unknown argument \"%s\"\n", argv[i]);
            print_usage_and_die();
        }
    }

    // Load file
    std::vector<std::uint8_t> file = { };
    {
        FILE* f = std::fopen(arg_file, "rb");
        if (!f) {
            std::fprintf(stderr, "Error: Failed to open file %s\n", arg_file);
            return EXIT_FAILURE;
        }
        std::fseek(f, 0, SEEK_END);
        file.resize(std::ftell(f));
        std::fseek(f, 0, SEEK_SET);
        std::fread(file.data(), 1, file.size(), f);
        std::fclose(f);
    }

    BinaryReader bin(file);

    // Read ELF file header
    ELF::Elf64_Ehdr ehdr = { };
    // Identity bits
    bin.read_bytes(ehdr.e_ident, sizeof(ehdr.e_ident));
    // Sanity check file
    if (std::memcmp(ehdr.e_ident, ELF::ElfMagic, 4) != 0) {
        std::fprintf(stderr, "Error: File is not an ELF object\n");
        return EXIT_FAILURE;
    }
    // Check if 32-bit
    bin.set_is_32(ehdr.e_ident[ELF::EI_CLASS] == ELF::ELFCLASS32);
    // Read remaining fields
    ehdr.e_type         = bin.read_u16();
    ehdr.e_machine      = bin.read_u16();
    ehdr.e_version      = bin.read_u32();
    ehdr.e_entry        = bin.read_ptr();
    ehdr.e_phoff        = bin.read_ptr();
    ehdr.e_shoff        = bin.read_ptr();
    ehdr.e_flags        = bin.read_u32();
    ehdr.e_ehsize       = bin.read_u16();
    ehdr.e_phentsize    = bin.read_u16();
    ehdr.e_phnum        = bin.read_u16();
    ehdr.e_shentsize    = bin.read_u16();
    ehdr.e_shnum        = bin.read_u16();
    ehdr.e_shstrndx     = bin.read_u16();

    // Helper: Read a section header
    static auto read_shdr = [&](ELF::Elf64_Shdr& shdr) {
        shdr.sh_name        = bin.read_u32();
        shdr.sh_type        = bin.read_u32();
        shdr.sh_flags       = bin.read_ptr();
        shdr.sh_addr        = bin.read_ptr();
        shdr.sh_offset      = bin.read_ptr();
        shdr.sh_size        = bin.read_ptr();
        shdr.sh_link        = bin.read_u32();
        shdr.sh_info        = bin.read_u32();
        shdr.sh_addralign   = bin.read_ptr();
        shdr.sh_entsize     = bin.read_ptr();
    };

    // Get section header string table
    bin.seek(ehdr.e_shoff + ehdr.e_shentsize * ehdr.e_shstrndx);
    ELF::Elf64_Shdr shstrtabhdr = { }; read_shdr(shstrtabhdr);
    assert(shstrtabhdr.sh_offset + shstrtabhdr.sh_size < file.size());
    const char* shstrtab = reinterpret_cast<const char*>(&file[shstrtabhdr.sh_offset]);

    // Need to look up section headers by name

    std::optional<ELF::Elf64_Shdr> sho_text;
    std::optional<ELF::Elf64_Shdr> sho_symtab;
    std::optional<ELF::Elf64_Shdr> sho_strtab;

    for (ELF::Elf64_Half i = 0; i < ehdr.e_shnum; ++i) {
        bin.seek(ehdr.e_shoff + ehdr.e_shentsize * i);
        ELF::Elf64_Shdr shdr = { }; read_shdr(shdr);

        assert(shdr.sh_name < shstrtabhdr.sh_size);
        const char* name = &shstrtab[shdr.sh_name];

        if (std::strcmp(name, ".text") == 0) {
            sho_text = shdr;
        } else if (std::strcmp(name, ".symtab") == 0) {
            sho_symtab = shdr;
        } else if (std::strcmp(name, ".strtab") == 0) {
            sho_strtab = shdr;
        }
    }

    if (!sho_text || !sho_symtab || !sho_strtab) {
        std::fprintf(stderr, "Error: File is missing one or more required sections\n");
        return EXIT_FAILURE;
    }

    // Iterate over symbols
    auto sh_text    = sho_text.value();
    auto sh_symtab  = sho_symtab.value();
    auto sh_strtab  = sho_strtab.value();

    assert(sh_strtab.sh_offset + sh_strtab.sh_size < file.size());
    const char* strtab = reinterpret_cast<const char*>(&file[sh_strtab.sh_offset]);

    // Read symbols
    std::vector<std::pair<ELF::Elf64_Sym, const char*>> syms(sh_symtab.sh_size / sh_symtab.sh_entsize);
    for (size_t i = 0; i < syms.size(); ++i) {
        ELF::Elf64_Sym& sym = syms[i].first;
        // Compiler should hoist this?
        if (ehdr.e_ident[ELF::EI_CLASS] == ELF::ELFCLASS32) {
            sym.st_name     = bin.read_u32();
            sym.st_value    = bin.read_ptr();
            sym.st_size     = bin.read_ptr();
            sym.st_info     = bin.read_u8();
            sym.st_other    = bin.read_u8();
            sym.st_shndx    = bin.read_u16();
        } else {
            sym.st_name     = bin.read_u32();
            sym.st_info     = bin.read_u8();
            sym.st_other    = bin.read_u8();
            sym.st_shndx    = bin.read_u16();
            sym.st_value    = bin.read_ptr();
            sym.st_size     = bin.read_ptr();
        }
        assert(sym.st_name < sh_strtab.sh_size);
        syms[i].second = &strtab[sym.st_name];
    }

    if (arg_json) {
        printf("{\n");
        printf("\t\"vtables\": [\n");
    }

    // Dump vtables
    char* demangle_buf = NULL;
    size_t demangle_buf_sz = 0;
    const size_t ptr_size = (ehdr.e_ident[ELF::EI_CLASS] == ELF::ELFCLASS32) ? sizeof(uint32_t) : sizeof(uint64_t);
    int table_number = 0;
    for (auto& sym : syms) {
        // Itanium-mangled VTables always start with _ZTV
        if (std::strncmp(sym.second, "_ZTV", 4) != 0) {
            continue;
        }

        // Demangle
        char* old_demangle_buf = demangle_buf;
        demangle_buf = llvm::itaniumDemangle(sym.second, demangle_buf, &demangle_buf_sz, NULL);
        if (!demangle_buf) {
            if (old_demangle_buf) {
                free(old_demangle_buf);
            }
            continue;
        }
        // Skip "vtable for "
        const char* classname = &demangle_buf[11];

        // Filter
        if (arg_filter.has_value() && !std::regex_match(classname, arg_filter.value())) {
            continue;
        }

        if (arg_json) {
            if (table_number > 0) {
                printf(",\n");
            }
            table_number++;
            printf("\t\t{\n");
            printf("\t\t\t\"classname\": \"%s\",\n", classname);
            if (arg_mangled) {
                printf("\t\t\t\"classname_mangled\": \"%s\",\n", sym.second);
            }
            printf("\t\t\t\"members\": [\n");
        } else {
            if (arg_mangled) {
                printf("VTable for %s (%s):\n", sym.second, classname);
            } else {
                printf("VTable for %s:\n", classname);
            }
        }

        // Walk table entries
        for (size_t i = 0; ; ++i) {
            size_t off = sym.first.st_value + (i + 3) * ptr_size;
            assert(off < file.size());

            // Read table entry
            bin.seek(off);
            uint64_t value = bin.read_ptr();

            // Make sure address points to valid code
            if (value < sh_text.sh_offset || value >= sh_text.sh_offset + sh_text.sh_size) {
                break;
            }

            // Get the symbol for this function
            auto func_sym = std::find_if(syms.begin(), syms.end(), [&](const auto& s) { return s.first.st_value == value; });
            if (func_sym == syms.end()) {
                continue;
            }

            // Demangle
            old_demangle_buf = demangle_buf;
            if (!(demangle_buf = llvm::itaniumDemangle(func_sym->second, demangle_buf, &demangle_buf_sz, NULL))) {
                if (old_demangle_buf) {
                    free(old_demangle_buf);
                }
                break;
            }

            if (arg_json) {
                if (i > 0) {
                    printf(",\n");
                }
                printf("\t\t\t\t{\n");
                if (arg_mangled) {
                    printf("\t\t\t\t\t\"name\": \"%s\",\n", demangle_buf);
                    printf("\t\t\t\t\t\"name_mangled\": \"%s\"\n", func_sym->second);
                } else {
                    printf("\t\t\t\t\t\"name\": \"%s\"\n", demangle_buf);
                }
                printf("\t\t\t\t}");
            } else {
                if (arg_mangled) {
                    printf("   %s    (%s)", func_sym->second, demangle_buf);
                } else {
                    printf("    %s\n", demangle_buf);
                }
            }
        }

        if (arg_json) {
            printf("\n\t\t\t]\n");
            printf("\t\t}");
        };
    }
    if (demangle_buf) {
        free(demangle_buf);
    }

    if (arg_json) {
        printf("\n\t]\n");
        printf("}\n");
    }

    return EXIT_SUCCESS;
}
