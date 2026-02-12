#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "dialogDic.h"
#include "quizDic.h"

typedef struct __attribute__((__packed__))
{
    uint16_t offsets[3];
} LANGUAGE_FILE;

typedef struct __attribute__((__packed__))
{
    uint8_t cmd;
    uint8_t length;
    char msg[];
} MESSAGE;

typedef struct __attribute__((__packed__))
{
    uint8_t count;
    MESSAGE start[];
} DIALOGUE;

#define TO_RAW 0x00
#define TO_ISO 0x01
#define TO_UTF 0x02
#define TO_WTF 0x04

static const char *lang[] = {"EN", "FR", "DE"};
// TODO: Make configurable:
static unsigned int convert = TO_UTF | TO_WTF;

/* Creates a directory recursively */
static void mkdirRecursive(const char *path)
{
    char *pos = (char *)path;
    while(1)
    {
        pos = strstr(pos, "/"); // TODO: Winblöd
        if(pos != NULL)
            *pos = '\0';

        mkdir(path, 0777);

        if(pos != NULL)
        {
            *pos = '/';
            pos++;
            continue;
        }
        break;
    }
}

/* Replaces characters from Rares character table with ISO-8859-1 */
static void transformRareToIso(char *string)
{
    while(1)
    {
        switch(*string)
        {
            case 0x5B: // Ä
                *string = 0xC4;
                break;
            case 0x5C: // Ö
                *string = 0xD6;
                break;
            case 0x5D: // Ü
            case 0x6A:
                *string = 0xDC;
                break;
            case 0x5E: // ß
                *string = 0xDF;
                break;
            case 0x5F: // À
                *string = 0xC0;
                break;
            case 0x60: // Â
                *string = 0xC2;
                break;
            case 0x61: // Ç
                *string = 0xC7;
                break;
            case 0x62: // É
                *string = 0xC9;
                break;
            case 0x63: // È
                *string = 0xC8;
                break;
            case 0x64: // Ê
                *string = 0xCA;
                break;
            case 0x65: // Ë
                *string = 0xCB;
                break;
            case 0x66: // Î
                *string = 0xCE;
                break;
            case 0x67: // Ï
                *string = 0xCF;
                break;
            case 0x68: // Ô
                *string = 0xD4;
                break;
            case 0x69: // Û
                *string = 0xDB;
                break;
            case 0x6B: // Ù
                *string = 0xD9;
                break;
            case '\0':
                return;
            default:
                break;
        }

        string++;
    }
}

/* Replaces characters from Rares character table with UTF-8 */
static char *transformRareToUtf(char *string)
{
    size_t sl = strlen(string) + 1;
    size_t j = 0;
    static uint8_t stringBuffer[256];
    for(size_t i = 0; i < sl; i++, j++)
    {
        if(j >= 254)
        {
            fprintf(stderr, "BUFFER OVERFLOW!\n");
            stringBuffer[254] = '\0';
            return (char *)stringBuffer;
        }

        switch(string[i])
        {
            case 0x5B:
                memcpy(stringBuffer + j++, "Ä", 2); // used sizeof("Ä") - 1 in the past but it's 2 for all chars anyway
                break;
            case 0x5C:
                memcpy(stringBuffer + j++, "Ö", 2);
                break;
            case 0x5D:
            case 0x6A:
                memcpy(stringBuffer + j++, "Ü", 2);
                break;
            case 0x5E:
                memcpy(stringBuffer + j++, "ß", 2);
                break;
            case 0x5F:
                memcpy(stringBuffer + j++, "À", 2);
                break;
            case 0x60:
                memcpy(stringBuffer + j++, "Â", 2);
                break;
            case 0x61:
                memcpy(stringBuffer + j++, "Ç", 2);
                break;
            case 0x62:
                memcpy(stringBuffer + j++, "É", 2);
                break;
            case 0x63:
                memcpy(stringBuffer + j++, "È", 2);
                break;
            case 0x64:
                memcpy(stringBuffer + j++, "Ê", 2);
                break;
            case 0x65:
                memcpy(stringBuffer + j++, "Ë", 2);
                break;
            case 0x66:
                memcpy(stringBuffer + j++, "Î", 2);
                break;
            case 0x67:
                memcpy(stringBuffer + j++, "Ï", 2);
                break;
            case 0x68:
                memcpy(stringBuffer + j++, "Ô", 2);
                break;
            case 0x69:
                memcpy(stringBuffer + j++, "Û", 2);
                break;
            case 0x6B:
                memcpy(stringBuffer + j++, "Ù", 2);
                break;
            case '\0':
                stringBuffer[j] = '\0';
                return (char *)stringBuffer;
            default:
                stringBuffer[j] = string[i];
                break;
        }
    }

    return string;
}

/*
 * Maps a .bin filename to the corresponding .dialog filename
 *
 * Expects a 6 char string as input
 * (null terminator not counted nor needed as it compares 6 bytes only)
 */
static const char *mapDialog(const char *in)
{
    for(size_t i = 0; i < DIAG_LIST_MAX; i++)
        if(memcmp(in, diagInList[i], 6) == 0)
            return diagOutList[i];

    return NULL;
}

static const char *mapQuiz(const char *in)
{
    for(size_t i = 0; i < QUIZ_LIST_MAX; i++)
        if(memcmp(in, quizInList[i], 6) == 0)
            return quizOutList[i];

    return NULL;
}

static const char *mapGrunty(const char *in)
{
    for(size_t i = 0; i < GRUNTY_LIST_MAX; i++)
        if(memcmp(in, gruntyInList[i], 6) == 0)
            return gruntyOutList[i];

    return NULL;
}

/*
 * Parse a .bin file representing a .quiz_q file
 *
 * This will map the .bin file to the corresponding .quiz_q file and create said .quiz_q file with YAML content.
 * It will write to stderr and skip the .bin file in case of no map entry (no .dialog file to write to known)
 */
static int parseQuiz(uint8_t *blob, const char *name, bool grunty)
{
    const char *outName = grunty ? mapGrunty(name) : mapQuiz(name);
    if(outName == NULL)
    {
        fprintf(stderr, "No map entry for quiz_q %s.bin\n", name);
        return 0;
    }

    // The path buffer for the files to write to. The Xes will be replaced later
    char outPath[grunty ? sizeof("XX/grunty_q/XXXX.grunty_q") : sizeof("XX/quiz_q/XXXX.quiz_q")];
    size_t pl;
    if(grunty)
    {
        memcpy(outPath, "XX/grunty_q/XXXX.grunty_q", sizeof("XX/grunty_q/XXXX.grunty_q"));
        pl = sizeof("XX/grunty_q/") - 1;
    }
    else
    {
        memcpy(outPath, "XX/quiz_q/XXXX.quiz_q", sizeof("XX/quiz_q/XXXX.quiz_q"));
        pl = sizeof("XX/quiz_q/") - 1;
    }

    // Replace XXXX with the file name
    memcpy(outPath + pl--, outName, 4);

    // Cast the blob into a LANGUAGE_FILE struct
    LANGUAGE_FILE *lf = (LANGUAGE_FILE *)(blob + 0x03);

    // Loop over the DIALOGUE structs to get count of and the pointers for the MESSAGE structs in the blob
    for(int i = 0; i < 3; i++)
    {
        // Replace the XX in out path buffer with the language (EN/FR/DE)
        memcpy(outPath, lang[i], 2);

        outPath[pl] = '\0';
        mkdirRecursive(outPath);
        outPath[pl] = '/';

        FILE *f = fopen(outPath, "wb");
        if(f == NULL)
        {
            fprintf(stderr, "Error opening %s\n", outPath);
            return 1;
        }

        fprintf(f, "type: QuizQuestion\n"
                   "question:\n");

        // Point and cast to the DIALOGUE structs found in the blob
        // Each DIALOGUE struct corresponds to one language (EN/FR/DE)
        DIALOGUE *diag = (DIALOGUE *)(blob + lf->offsets[i]);

        MESSAGE *msg = diag->start;
        bool firstAnswer = true;

        // Loop over the MESSAGE structs found
        // Parse them and write out as YAML
        for(uint8_t j = 0; j < diag->count; j++)
        {
            if(firstAnswer && msg->cmd & ~(0x80))
            {
                fprintf(f, "options:\n");
                firstAnswer = false;
            }

            fprintf(f, "  - { cmd: 0x%02X, string: \"", msg->cmd);

            // TODO: WTF?
            if(!firstAnswer && (convert & TO_WTF))
                fprintf(f, "\\xFDl");

            char *m = msg->msg;
            if(convert & TO_ISO)
                transformRareToIso(m);
            else if(convert & TO_UTF)
                m = transformRareToUtf(m);

            fprintf(f, "%s\" }\n", m);
            msg = (MESSAGE *)(((uint8_t *)msg) + 2 + msg->length);
        }

        fclose(f);
    }

    return 0;
}

// Transforms a 4 character (excluding null terminator) hex string to bytes
static uint32_t hexHex(const char *name)
{
    uint32_t ret = 0;
    for(unsigned int i = 0; i < 4; i++)
    {
        uint32_t c = name[3 - i];
        if(c == '0')
            continue;

        // Lowercase to Uppercase
        if(c >= 'a')
            c -= 0x20;

        // Map 'A' behind '9'
        if(c >= 'A')
            c -= 0x07;

        // map '1' to 0x01
        c -= 0x30;

        c <<= 4 * i;
        ret += c;
    }

    return ret;
}

/*
 * Parse a .bin file representing a .dialog file
 *
 * This will map the .bin file to the corresponding .dialog file and create said .dialog file with YAML content.
 * It will write to stderr and skip the .bin file in case of no map entry (no .dialog file to write to known)
 */
static int parseDialog(uint8_t *blob, const char *name)
{
    const char *outName = mapDialog(name);
    if(outName == NULL)
    {
        fprintf(stderr, "No map entry for dialog %s.bin\n", name);
        return 0;
    }

    char outPath[] = "XX/dialog/XXXX.dialog";
    memcpy(outPath + sizeof("XX/dialog/") - 1, outName, 4);

    LANGUAGE_FILE *lf = (LANGUAGE_FILE *)(blob + 0x01);

    uint32_t hex = hexHex(outName);
    bool brentilda = hex >= 0x1083 && hex <= 0x1099;

    for(int i = 0; i < 3; i++)
    {
        memcpy(outPath, lang[i], 2);
//        printf("--> %s\n", outPath);

        outPath[sizeof("XX/dialog") - 1] = '\0';
        mkdirRecursive(outPath);
        outPath[sizeof("XX/dialog") - 1] = '/';

        // Open the .dialog file for writing and write YAML to it
        FILE *f = fopen(outPath, "wb");
        if(f == NULL)
        {
            fprintf(stderr, "Error opening %s\n", outPath);
            return 1;
        }

        // Loop over bottom messages
        fprintf(f, "type: Dialog\n"
                   "bottom:\n");
        DIALOGUE *diag = (DIALOGUE *)(blob + lf->offsets[i]);
        MESSAGE *msg = diag->start;
        uint8_t count = diag->count;
        for(uint8_t j = 0; j < count; j++)
        {
            char *m = msg->msg;
            if(brentilda || (msg->cmd & 0x80))
            {
                if(convert & TO_ISO)
                    transformRareToIso(m);
                else if(convert & TO_UTF)
                    m = transformRareToUtf(m);
            }

            fprintf(f, "  - { cmd: 0x%02X, string: \"%s\" }\n", msg->cmd, m);
            msg = (MESSAGE *)(((uint8_t *)msg) + 2 + msg->length);
        }

        // Loop over top messages
        fprintf(f, "top:\n");
        count = *(uint8_t *)msg;
        msg = (MESSAGE *)(((uint8_t *)msg) + 0x01);
        for(uint8_t j = 0; j < count; j++)
        {
            char *m = msg->msg;
            if(msg->cmd & 0x80)
            {
                if(convert & TO_ISO)
                    transformRareToIso(m);
                else if(convert & TO_UTF)
                    m = transformRareToUtf(m);
            }

            fprintf(f, "  - { cmd: 0x%02X, string: \"%s\" }\n", msg->cmd, m);
            msg = (MESSAGE *)(((uint8_t *)msg) + 2 + msg->length);
        }

        // Close output file
        fclose(f);
    }

    return 0;
}

/*
 * This processes a .bin file
 *
 * So it prses its file magic (first two bytes) to decide if dialog or quiz_q,
 * reads the file into a buffer and handles that blob to the corresponding parser function
 */
static int process(const char *name, const char *file)
{
    int ret = 1;

    // Open file
    FILE *f = fopen(file, "rb");
    if(f)
    {
        // Get filesize from open file
        if(fseek(f, 0L, SEEK_END) == 0)
        {
            size_t filesize = ftell(f);
            if(filesize > 32 && filesize < 4096)
            {
                // Create buffer and read file into it
                if(fseek(f, 0L, SEEK_SET) == 0)
                {
                    uint8_t blob[filesize];
                    if(fread(blob, filesize, 1, f) == 1)
                    {
                        uint16_t magic = *(uint16_t *)blob;
                        bool grunty = false;
                        switch(magic)
                        {
                            case 0x0703: // .dialog
                                ret = parseDialog(blob, name);
                                break;
                            case 0x0303: // .grunty_q
                                grunty = true;
                            case 0x0103: // .quiz_q
                                ret = parseQuiz(blob, name, grunty);
                                break;
                            default:
                                fprintf(stderr, "Unknown file magic for %s: 0x%04X\n", file, magic);
                        }
                    }
                    else
                        fprintf(stderr, "Error reading %s\n", file);
                }
                else
                    fprintf(stderr, "I/O error: %s (%u)\n", strerror(errno), errno);
            }
            else
                fprintf(stderr, "Sanity error (%s)\n", file);
        }
        else
            fprintf(stderr, "I/O error: %s (%u)\n", strerror(errno), errno);

        // Close input file
        fclose(f);
    }
    else
        fprintf(stderr, "%s not found\n", file);

    return ret;
}

static void showHelp(char *prog)
{
    fprintf(stderr, "Usage: %s [-u|-i|-r]  [-w|-n] input/path\n"
                    "\t-u: Convert strings to UTF-8 (default)\n"
                    "\t-i: Convert strings to ISO-8859-1\n"
                    "\t-r: Dump strings raw (RARE character table)\n"
                    "\t-w: Add weird bytes (\"\\xFDl\") to beginning of answers (needed by Banjo: Recompiled but missing in PAL ROM) (default)\n"
                    "\t-n: Don't add weird bytes (see above)\n", prog);
}

/*
 * Entry function of the program
 *
 * It accepts exactly one argument: A path containing .bin files
 */
int main(int argc, char *argv[])
{
    // Check if argument is there
    if(argc < 2)
    {
        showHelp(argv[0]);
        return 1;
    }

    const char *path = argv[--argc];
    if(argc > 1)
    {
        for(int i = 1; i < argc; i++)
        {
            if(argv[i][0] != '-')
            {
                showHelp(argv[0]);
                return 1;
            }

            switch(argv[i][1])
            {
                case 'i':
                    convert &= ~(TO_UTF);
                    convert |= TO_ISO;
                    break;
                case 'u':
                    convert &= ~(TO_ISO);
                    convert |= TO_UTF;
                    break;
                case 'r':
                    convert &= ~(TO_UTF | TO_ISO);
                    break;
                case 'w':
                    convert |= TO_WTF;
                    break;
                case 'n':
                    convert &= ~(TO_WTF);
                    break;
                default:
                    showHelp(argv[0]);
                    return 1;
            }

            if(argv[i][2] != '\0')
            {
                showHelp(argv[0]);
                return 1;
            }
        }
    }

    if(convert & TO_UTF)
        printf("Converting strings to UTF-8");
    else if(convert & TO_ISO)
        printf("Converting strings to ISO-8859-1");
    else
        printf("Dumping strings raw (RARE character table)");

    if(convert & TO_WTF)
        printf(" (adding weird bytes to answers)");

    printf("\n");

    // Open directory
    DIR *folder = opendir(path);
    if(!folder)
    {
        fprintf(stderr, "Error opening %s\n", path);
        return 1;
    }

    // Loop over all files in the folder, create a new path array containing the folder + the filename and process the files
    size_t sl = strlen(path);
    char newPath[sl + (1 + 6 + 1 + 3 + 1)]; // path + '/' + filename + '.' + extension + '\0'
    memcpy(newPath, path, sl);
    newPath[sl++] = '/';
    newPath[sl + (6 + 1 + 3)] = '\0'; // Add null terminator to end of buffer as memcpy will not copy it later
    struct dirent *entry;
    int ret = 0;
    while (ret == 0 && (entry = readdir(folder)) != NULL) {
        // Check if file is not hidden, is a real file, filename is 10 chars long (including extension) and extension is .bin. Skip otherwise
        if(entry->d_name[0] == '.' || entry->d_type != DT_REG || strlen(entry->d_name) != 6 + 1 + 3 /* filename + '.' + extension */ || memcmp(entry->d_name + 6, ".bin", 4) != 0)
            continue;

        // Copy filename to end of new path
        memcpy(newPath + sl, entry->d_name, 6 + 1 + 3); // filename + '.' + extension

        // Cut extension from filename
        entry->d_name[6] = '\0';
        ret = process(entry->d_name, newPath);
    }

    // Close the folder and exit the program
    closedir(folder);
    if(ret == 0)
        printf("Done\n");

    return ret;
}
