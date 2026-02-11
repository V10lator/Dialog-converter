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

static const char *lang[] = {"EN", "FR", "DE"};

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
        }

        string++;
    }
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

/*
 * Parse a .bin file representing a .quiz_q file
 *
 * Note that there is no quiz_q map currently, so this function just prints to CLI instead of creating files for now
 */
static int parseQuiz(uint8_t *blob, const char *name)
{
    const char *outName = mapQuiz(name);
    if(outName == NULL)
    {
        fprintf(stderr, "No map entry for quiz_q %s.bin\n", name);
        return 0;
    }

    // The path buffer for the files to write to. The Xes will be replaced later
    char outPath[] = "XX/quiz_q/XXXX.quiz_q";
    // Replace XXXX with the file name
    memcpy(outPath + sizeof("XX/quiz_q/") - 1, outName, 4);

    // Cast the blob into a LANGUAGE_FILE struct
    LANGUAGE_FILE *lf = (LANGUAGE_FILE *)(blob + 0x03);

    // Loop over the DIALOGUE structs to get count of and the pointers for the MESSAGE structs in the blob
    for(int i = 0; i < 3; i++)
    {
        // Replace the XX in out path buffer with the language (EN/FR/DE)
        memcpy(outPath, lang[i], 2);

        outPath[sizeof("XX/quiz_q") - 1] = '\0';
        mkdirRecursive(outPath);
        outPath[sizeof("XX/quiz_q") - 1] = '/';

        FILE *f = fopen(outPath, "wb");
        if(f == NULL)
        {
            fprintf(stderr, "Error opening %s\n", outPath);
            return 1;
        }

        fprintf(f, "type: QuizQuestion\n");
        fprintf(f, "question:\n");

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

            transformRareToIso(msg->msg);
            fprintf(f, "  - { cmd: 0x%02X, string: \"", msg->cmd);

            // TODO: WTF?
            if(!firstAnswer)
                fprintf(f, "\\xFDl");

            fprintf(f, "%s\" }\n", msg->msg);
            msg = (MESSAGE *)(((uint8_t *)msg) + 2 + msg->length);
        }

        fclose(f);
    }

    return 0;
}

/*
 * Parse a .bin file representing a .dialog file
 *
 * This will map the .bin file to the corresponding .dialog file and create said .dialog file with YAML content.
 * It will write to CLI and skip the .bin file in case of no map entry (no .dialog file to write to known)
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
        fprintf(f, "type: Dialog\n");
        fprintf(f, "bottom:\n");
        DIALOGUE *diag = (DIALOGUE *)(blob + lf->offsets[i]);
        MESSAGE *msg = diag->start;
        uint8_t count = diag->count;
        for(uint8_t j = 0; j < count; j++)
        {
            if(msg->cmd & 0x80)
                transformRareToIso(msg->msg);

            fprintf(f, "  - { cmd: 0x%02X, string: \"%s\" }\n", msg->cmd, msg->msg);
            msg = (MESSAGE *)(((uint8_t *)msg) + 2 + msg->length);
        }

        // Loop over top messages
        fprintf(f, "top:\n");
        count = *(uint8_t *)msg;
        msg = (MESSAGE *)(((uint8_t *)msg) + 0x01);
        for(uint8_t j = 0; j < count; j++)
        {
            if(msg->cmd & 0x80)
                transformRareToIso(msg->msg);

            fprintf(f, "  - { cmd: 0x%02X, string: \"%s\" }\n", msg->cmd, msg->msg);
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
                        switch(magic)
                        {
                            case 0x0703: // .dialog
                                ret = parseDialog(blob, name);
                                break;
                            case 0x0303: // .quiz_q
                            case 0x0103:
                                ret = parseQuiz(blob, name);
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
        fprintf(stderr, "Usage: %s input/path\n", argv[0]);
        return 1;
    }

    // Open directory
    DIR *folder = opendir(argv[1]);
    if(!folder)
    {
        fprintf(stderr, "Error opening %s\n", argv[1]);
        return 1;
    }

    // Loop over all files in the folder, create a new path array containing the folder + the filename and process the files
    size_t sl = strlen(argv[1]);
    char newPath[sl + (1 + 6 + 1 + 3 + 1)]; // path + '/' + filename + '.' + extension + '\0'
    memcpy(newPath, argv[1], sl);
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
        printf("Done");

    return ret;
}
