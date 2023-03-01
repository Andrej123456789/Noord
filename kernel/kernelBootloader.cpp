#include <stdint.h>
#include <stddef.h>
#include "../limine/limine.h"

struct Framebuffer
{
    void *BaseAddress;
    size_t BufferSize;
    unsigned int Width;
    unsigned int Height;
    unsigned int PixelsPerScanLine;
};

struct RSDP1
{
    unsigned char Signature[8];
    uint8_t Checksum;
    uint8_t OEM_ID[6];
    uint8_t Revision;
    uint32_t RSDTAddress;
} __attribute__((packed));

struct RSDP2
{
    RSDP1 firstPart;

    uint32_t Length;
    uint64_t XSDTAddress;
    uint8_t ExtendedChecksum;
    uint8_t Reserved[3];

} __attribute__((packed));

#define PSF1_MAGIC0 0x36
#define PSF1_MAGIC1 0x04

struct PSF1_HEADER
{
    unsigned char magic[2];
    unsigned char mode;
    unsigned char charsize;
};

struct PSF1_FONT
{
    PSF1_HEADER *psf1_Header;
    void *glyphBuffer;
};

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent.

static volatile struct limine_terminal_request terminal_request = {
    .id = LIMINE_TERMINAL_REQUEST,
    .revision = 0};

static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0};

static volatile limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST,
    .revision = 0};

static volatile limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0};

static volatile limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0};

// #define LIMINE_5_LEVEL_PAGING_REQUEST { LIMINE_COMMON_MAGIC, 0x94469551da9b3192, 0xebe5e86db7382888}

// static volatile limine_5_level_paging_request paging_request = {
//     .id = LIMINE_5_LEVEL_PAGING_REQUEST,
//     .revision = 0
// };

// #define LIMINE_ENTRY_POINT_REQUEST { LIMINE_COMMON_MAGIC, 0x13d86c035a1cd3e1, 0x2b0caa89d8f3026a }

// extern "C" void _start(void);

// static volatile limine_entry_point_request entry_request = {
//     .id = LIMINE_ENTRY_POINT_REQUEST,
//     .revision = 0,
//     .entry = _start
// };

struct limine_kernel_address_request kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0,
    .response = NULL};

//__attribute__((section(".limine_reqs")))

static void done(void)
{
    for (;;)
    {
        __asm__("hlt");
    }
}

#include "e9print.h"

static void print_file(struct limine_file *file)
{
    e9_printf("File->Revision: %d", file->revision);
    e9_printf("File->Address: %x", file->address);
    e9_printf("File->Size: %x", file->size);
    e9_printf("File->Path: %s", file->path);
    e9_printf("File->CmdLine: %s", file->cmdline);
    e9_printf("File->MediaType: %d", file->media_type);
    e9_printf("File->PartIndex: %d", file->partition_index);
    e9_printf("File->TFTPIP: %d.%d.%d.%d",
              (file->tftp_ip & (0xff << 0)) >> 0,
              (file->tftp_ip & (0xff << 8)) >> 8,
              (file->tftp_ip & (0xff << 16)) >> 16,
              (file->tftp_ip & (0xff << 24)) >> 24);
    e9_printf("File->TFTPPort: %d", file->tftp_port);
    e9_printf("File->MBRDiskId: %x", file->mbr_disk_id);
    e9_printf("File->GPTDiskUUID: %x-%x-%x-%x",
              file->gpt_disk_uuid.a,
              file->gpt_disk_uuid.b,
              file->gpt_disk_uuid.c,
              *(uint64_t *)file->gpt_disk_uuid.d);
    e9_printf("File->GPTPartUUID: %x-%x-%x-%x",
              file->gpt_part_uuid.a,
              file->gpt_part_uuid.b,
              file->gpt_part_uuid.c,
              *(uint64_t *)file->gpt_part_uuid.d);
    e9_printf("File->PartUUID: %x-%x-%x-%x",
              file->part_uuid.a,
              file->part_uuid.b,
              file->part_uuid.c,
              *(uint64_t *)file->part_uuid.d);
}

static const char *get_memmap_type(uint64_t type)
{
    switch (type)
    {
    case LIMINE_MEMMAP_USABLE:
        return "Usable";
    case LIMINE_MEMMAP_RESERVED:
        return "Reserved";
    case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
        return "ACPI reclaimable";
    case LIMINE_MEMMAP_ACPI_NVS:
        return "ACPI NVS";
    case LIMINE_MEMMAP_BAD_MEMORY:
        return "Bad memory";
    case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
        return "Bootloader reclaimable";
    case LIMINE_MEMMAP_KERNEL_AND_MODULES:
        return "Kernel and modules";
    case LIMINE_MEMMAP_FRAMEBUFFER:
        return "Framebuffer";
    default:
        return "???";
    }
}

static void write_shim(const char *s, uint64_t l)
{
    struct limine_terminal *terminal = terminal_request.response->terminals[0];

    terminal_request.response->write(terminal, s, l);
}

void *startRAMAddr = NULL;
void *quickMalloc(uint64_t size)
{
    if (startRAMAddr == NULL)
    {
        e9_printf("> Cannot malloc from NULL!");
        done();
    }
    void *temp = startRAMAddr;
    startRAMAddr = (void *)((uint64_t)startRAMAddr + size);
    return temp;
}

bool checkStringEndsWith(const char *str, const char *end)
{
    const char *_str = str;
    const char *_end = end;

    while (*str != 0)
        str++;
    str--;

    while (*end != 0)
        end++;
    end--;

    while (true)
    {
        if (*str != *end)
            return false;

        str--;
        end--;

        if (end == _end || (str == _str && end == _end))
            return true;

        if (str == _str)
            return false;
    }

    return true;
}

limine_file *getFile(const char *name)
{
    limine_module_response *module_response = module_request.response;
    for (size_t i = 0; i < module_response->module_count; i++)
    {
        limine_file *f = module_response->modules[i];
        if (checkStringEndsWith(f->path, name))
            return f;
    }
    return NULL;
}

// The following will be our kernel's entry point.
extern "C" void _start(void)
{
    // Ensure we got a terminal
    if (terminal_request.response == NULL || terminal_request.response->terminal_count < 1)
    {
        done();
    }

    limine_print = write_shim;

    // We should now be able to call the Limine terminal to print out
    // a simple "Hello World" to screen.
    struct limine_terminal *terminal = terminal_request.response->terminals[0];
    terminal_request.response->write(terminal, "LIMINE_INFO: Starting Boot init...\n", 24);

    if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1)
    {
        terminal_request.response->write(terminal, "LIMINE_ERROR: Framebuffer is NULL!\n", 23);
        done();
    }
    else
        terminal_request.response->write(terminal, "LIMINE_ERROR: Framebuffer loaded!\n", 22);

    if (rsdp_request.response == NULL || rsdp_request.response->address == NULL)
    {
        terminal_request.response->write(terminal, "LIMINE_ERROR: RSDP is NULL!\n", 23);
        done();
    }
    else
        terminal_request.response->write(terminal, "LIMINE_SUCCESS: RSDP loaded!\n", 15);

    Framebuffer fb;
    {
        limine_framebuffer *lFb = framebuffer_request.response->framebuffers[0];
        fb.BaseAddress = lFb->address;
        fb.Width = lFb->width;
        fb.Height = lFb->height;
        fb.PixelsPerScanLine = lFb->pitch / 4;
        fb.BufferSize = lFb->height * lFb->pitch; // lFb->edid_size;
    }
    RSDP2 *rsdp;
    {
        rsdp = (RSDP2 *)rsdp_request.response->address;
    }

    if (memmap_request.response == NULL)
    {
        e9_printf("LIMINE_ERROR: Memory map not passed");
        done();
    }

    void *freeMemStart = NULL;
    uint64_t freeMemSize = 0;

    if (kernel_address_request.response == NULL)
    {
        e9_printf("LIMINE_ERROR: Kernel address not passed");
        done();
    }
    struct limine_kernel_address_response *ka_response = kernel_address_request.response;
    e9_printf("Kernel address feature, revision %d", ka_response->revision);
    e9_printf("Physical base: %x", ka_response->physical_base);
    e9_printf("Virtual base: %x", ka_response->virtual_base);

    void *kernelStart = (void *)ka_response->physical_base;
    void *kernelStartV = (void *)ka_response->virtual_base;
    uint64_t kernelSize = 1;

    limine_memmap_response *memmap_response = memmap_request.response;
    // e9_printf("Memory map feature, revision %d", memmap_response->revision);
    // e9_printf("%d memory map entries", memmap_response->entry_count);
    for (size_t i = 0; i < memmap_response->entry_count; i++)
    {
        limine_memmap_entry *e = memmap_response->entries[i];
        // e9_printf("> %x->%x %s", e->base, e->base + e->length, get_memmap_type(e->type));
        if (e->type == LIMINE_MEMMAP_USABLE)
        {
            // e9_printf("%x->%x %s  (%d %d %d %d bytes)", e->base, e->base + e->length, get_memmap_type(e->type), (e->length / 1000000000), (e->length / 1000000) % 1000, (e->length / 1000) % 1000, e->length % 1000);
            if (e->length > freeMemSize)
            {
                freeMemStart = (void *)e->base;
                freeMemSize = e->length;
            }
        }
        else if (e->base == (uint64_t)kernelStart)
        {
            kernelSize = e->length;
        }
    }
    if (freeMemStart == NULL)
    {
        e9_printf("LIMINE_ERROR: No valid Memory space found for OS!");
        done();
    }
    startRAMAddr = freeMemStart;

    if (module_request.response == NULL)
    {
        e9_printf("LIMINE_ERROR: Modules not passed!");
        done();
    }
    else
    {
        // limine_module_response *module_response = module_request.response;
        // e9_printf("Modules feature, revision %d", module_response->revision);
        // e9_printf("%d module(s)", module_response->module_count);
        // for (size_t i = 0; i < module_response->module_count; i++) {
        //     limine_file *f = module_response->modules[i];
        //     e9_printf("---");
        //     print_file(f);
        // }
    }

    PSF1_FONT font;
    {
        const char *fName = "zap-light16.psf";
        limine_file *file = getFile(fName);
        if (file == NULL)
        {
            e9_printf("LIMINE_ERROR: Failed to get Font \"%s\"!", fName);
            done();
        }

        font.psf1_Header = (PSF1_HEADER *)file->address;
        if (font.psf1_Header->magic[0] != 0x36 || font.psf1_Header->magic[1] != 0x04)
        {
            e9_printf("LIMINE_ERROR: FONT HEADER INVALID!");
            done();
        }

        font.glyphBuffer = (void *)((uint64_t)file->address + sizeof(PSF1_HEADER));
    }

    e9_printf("LIMINE_SUCCESS: Assets loaded!");

    {
        uint64_t mallocDiff = (uint64_t)startRAMAddr - (uint64_t)freeMemStart;

        e9_printf("");
        e9_printf("LIMINE_INFO: DEBUG RAM STATS");
        e9_printf("LIMINE_INFO: %x (NOW) - %x (START) = %d bytes malloced", (uint64_t)startRAMAddr, (uint64_t)freeMemStart, mallocDiff);
        e9_printf("");

        // freeMemStart = startRAMAddr;
        freeMemSize -= mallocDiff;
    }

    e9_printf("LIMINE_INFO: Kernel Start: %x (Size: %d Bytes)", (uint64_t)kernelStart, kernelSize);
    e9_printf("");
    e9_printf("LIMINE_INFO: OS has %d MB of RAM. (Starts at %x)", freeMemSize / 1000000, (uint64_t)freeMemStart);

    terminal_request.response->write(terminal, "> Completed Boot Init!\n", 23);
    /* call ringOSX kernel */

    done();
}
