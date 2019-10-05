#include "osoyanlib/osoyan.h"

#define SEARCH_BY_ALL 1u
#define SEARCH_BY_TITLE 2u
#define SEARCH_BY_DESC 4u
#define SEARCH_BY_TAGS 8u
#define SEARCH_BY_SECTION 16u
#define PRINT_FIRST_RESULT 32u

struct DocumentSection {
    size_t start;
    size_t end;
    char name[64];
};

struct DocumentInfo {
    char path[512];
    char title[255];
    char description[512];
    struct StringArray *tags;
    struct Vector *sectionList;
};

static size_t stackDocumentId = 0;
bool echoIsEnabled = true;
bool lastStatus = true;

void destroy_documents(struct Vector *documentList) {
    for (size_t i = 0; i < documentList->length; ++i) {
        struct DocumentInfo *document = documentList->list[i];
        DESTROY_STRING_ARRAY(document->tags)
        MEMORY_FREE(document)
    }
    DESTROY_VECTOR(documentList);
}

struct Vector *analyze_doc_folder(char *path) {
    // Get all txt files from docs folder
    EQU_VECTOR(docList) = file_search(path, "^[a-zA-Z0-9_]+\\.txt$", 0);
    NEW_VECTOR(documentInfoList, struct DocumentInfo *)

    for (size_t i = 0; i < docList->length; ++i) {
        // Get first 255 bytes of each file for analyze
        struct FileInfo *fileInfo = (struct FileInfo *) (docList->list[i]);
        EQU_BLOB(fileData) = file_get_contents(fileInfo->path);

        // New document info
        struct DocumentInfo *newDoc = MEMORY_ALLOCATE_STRUCT(DocumentInfo);
        chars_set(newDoc->path, fileInfo->path, sizeof(newDoc->path));
        INIT_VECTOR(newDoc->sectionList, struct DocumentSection *)

        // Parse lines
        EQU_STRING_ARRAY(lines) = chars_split((char *) fileData->list, "\n", 0);
        bool isHeaderMode = true;

        size_t charsOffset = 0;

        for (size_t j = 0; j < lines->length; ++j) {
            char *line = lines->list[j]->list;

            // Break if end of headers
            if (lines->list[j]->length == 0) {
                isHeaderMode = false;
                charsOffset += 1;
                continue;
            }

            // Header mode
            if (isHeaderMode) {
                // Not a header
                if (lines->list[j]->list[0] != '#') {
                    printf("Unknown header '%s' in file '%s'", lines->list[j]->list, fileInfo->path);
                    continue;
                }

                // Parse header
                EQU_STRING_ARRAY(X3) = chars_split(lines->list[j]->list + 2, ": ", 0);

                // Set title
                if (X3->length > 1 && CHARS_EQUAL(X3->list[0]->list, "Title"))
                    chars_set(newDoc->title, X3->list[1]->list, sizeof(newDoc->title));

                // Set desc
                if (X3->length > 1 && CHARS_EQUAL(X3->list[0]->list, "Desc"))
                    chars_set(newDoc->description, X3->list[1]->list, sizeof(newDoc->description));

                // Set tags
                if (X3->length > 1 && CHARS_EQUAL(X3->list[0]->list, "Tags")) {
                    EQU_STRING_ARRAY(tags) = chars_split(X3->list[1]->list, ", ", 0);
                    newDoc->tags = tags;
                }

                // Destroy garbage
                DESTROY_STRING_ARRAY(X3)
            } else {
                // Content mode
                if (strncmp(line, "### SECTION: ", 13) == 0) {
                    struct DocumentSection *documentSection = MEMORY_ALLOCATE_STRUCT(DocumentSection);

                    // Name of section
                    for (size_t k = charsOffset + 13; k < fileData->length - 1; ++k) {
                        if (fileData->list[k] == ',' || fileData->list[k] == '\n') {
                            char *sectionName = chars_substr((char *)fileData->list, charsOffset + 13, k);
                            chars_set(documentSection->name, sectionName, sizeof(documentSection->name));
                            MEMORY_FREE(sectionName)
                            break;
                        }
                    }

                    // Search start of document
                    for (size_t k = charsOffset; k < fileData->length - 1; ++k) {
                        if (fileData->list[k] == '\n' && fileData->list[k + 1] == '\n') {
                            documentSection->start = k + 2;
                            break;
                        }
                    }

                    // Search end of document
                    documentSection->end = fileData->length;
                    for (size_t k = documentSection->start; k < fileData->length - 3; ++k) {
                        if (fileData->list[k] == '#' && fileData->list[k + 1] == '#'
                        && fileData->list[k + 2] == '#' && fileData->list[k + 3] == ' ') {
                            documentSection->end = k - 1;
                            break;
                        }
                    }

                    // Without this is dangerous
                    if (documentSection->start > documentSection->end)
                        documentSection->start = documentSection->end;

                    // charsOffset += (documentSection->end - documentSection->start);

                    // Push slice info
                    vector_push(newDoc->sectionList, documentSection);

                    // continue;
                }
            }

            charsOffset += strlen(line) + 1; // + \n
        }

        vector_push(documentInfoList, newDoc);

        // Destroy garbage
        DESTROY_STRING_ARRAY(lines)
        DESTROY_BLOB(fileData)
    }

    // Destroy garbage
    DESTROY_FILE_SEARCH_RESULT(docList)

    return documentInfoList;
}

void search_by_all(struct Vector *documentList, char *query, uint32_t flags) {
    lastStatus = false;

    for (size_t i = 0; i < documentList->length; ++i) {
        struct DocumentInfo *documentInfo = documentList->list[i];

        // Match by title, desc or tags
        bool isMatch = false;
        char *matchedBy = "";
        char *matchedValue = "";
        size_t sectionId = 0;

        // Try title
        if (chars_match(documentInfo->title, query, REG_ICASE) && (flags & SEARCH_BY_ALL || flags & SEARCH_BY_TITLE)) {
            matchedBy = "Title";
            matchedValue = documentInfo->title;
            isMatch = true;
        }

        // Try desc
        if (!isMatch && chars_match(documentInfo->description, query, REG_ICASE) && (flags & SEARCH_BY_ALL || flags & SEARCH_BY_DESC)) {
            matchedBy = "Desc";
            matchedValue = "...";
            isMatch = true;
        }

        // Try tags
        if (!isMatch) {
            for (size_t j = 0; j < documentInfo->tags->length; ++j) {
                // Check tag is matched
                if (chars_match(documentInfo->tags->list[j]->list, query, REG_ICASE) && (flags & SEARCH_BY_ALL || flags & SEARCH_BY_TAGS)) {
                    isMatch = true;
                    matchedBy = "Tags";
                    matchedValue = documentInfo->tags->list[j]->list;
                    break;
                }
            }
        }

        // Try section
        if (!isMatch) {
            for (size_t j = 0; j < documentInfo->sectionList->length; ++j) {
                struct DocumentSection *section = documentInfo->sectionList->list[j];

                // Check tag is matched
                if (chars_match(section->name, query, REG_ICASE) && (flags & SEARCH_BY_ALL || flags & SEARCH_BY_SECTION)) {
                    isMatch = true;
                    matchedBy = "Section";
                    sectionId = j;
                    matchedValue = section->name;
                    break;
                }
            }
        }

        if ((flags & PRINT_FIRST_RESULT) && isMatch) {
            EQU_BLOB(file) = file_get_contents(documentInfo->path);

            // Print section
            if (CHARS_EQUAL(matchedBy, "Section")) {
                struct DocumentSection *documentSection = documentInfo->sectionList->list[sectionId];
                char *part = chars_substr((char *)file->list, documentSection->start, documentSection->end);
                if (echoIsEnabled) printf("%s\n", part);
                MEMORY_FREE(part)
            } else {
                // Search start of file
                for (size_t j = 0; j < documentInfo->sectionList->length; ++j) {
                    struct DocumentSection *documentSection = documentInfo->sectionList->list[j];
                    if (echoIsEnabled) printf("SECTION %s [%zu:%zu]\n" "\n", documentSection->name, documentSection->start, documentSection->end);
                    char *part = chars_substr((char *)file->list, documentSection->start, documentSection->end);
                    if (echoIsEnabled) printf("%s\n", part);
                    MEMORY_FREE(part)
                }
            }

            lastStatus = true;
            stackDocumentId = i + 1;

            return;
        }

        // Document is matched
        if (isMatch) {
            stackDocumentId = i + 1;
            lastStatus = true;

            if (echoIsEnabled) printf("#%zu %s (%s) - Matched by %s \n",
                   i + 1,
                   documentInfo->path,
                   matchedValue,
                   matchedBy);
        }
    }
}

int main(int argc, char **argv) {
    MEMORY_INIT

    // Get args
    EQU_ARGS(argList) = args_init(argc, argv);
    bool isInteractiveMode = args_has_flags(argList, "i");
    char *startCmd = args_get_key_value(argList, "query");
    if (!startCmd) startCmd = "";

    char *homeDir = os_home_dir("docs/");
    struct Vector *docList = analyze_doc_folder(homeDir);
    struct StringArray *cmdStack = 0;
    if (isInteractiveMode) {
        printf("Loaded %zu documents\n", docList->length);
    } else {
        cmdStack = chars_split(startCmd, " && ", 0);
    }
    // MEMORY_PRINT_STATE

    // Command loop
    while (true) {
        char cmd[32];

        if (isInteractiveMode) {
            printf("> ");
            fgets(cmd, 32, stdin);
            if (cmd[0] == '\n') continue;
            if (cmd[strlen(cmd) - 1] == '\n') cmd[strlen(cmd) - 1] = 0;
        } else {
            /*if (startCmd) {
                chars_set(cmd, cmdStack->list[0]->list, sizeof(cmd));
                string_array_remove_at(cmdStack, 0, 0);
            }
            else chars_set(cmd, "l", sizeof(cmd));*/
            if (cmdStack->length > 0) {
                bzero(cmd, sizeof(cmd));
                chars_set(cmd, cmdStack->list[0]->list, sizeof(cmd));
                string_array_remove_at(cmdStack, 0, 1);
                // printf("%s\n", cmd);
            } else {
                break;
            }
        }

        NEW_STRING(X)
        string_add(X, "%d", stackDocumentId);
        char *replaceCmd = chars_replace(cmd, "%id", X->list);
        bzero(cmd, sizeof(cmd));
        chars_set(cmd, replaceCmd, sizeof(cmd));
        MEMORY_FREE(replaceCmd)
        DESTROY_STRING(X)


        EQU_STRING_ARRAY(cmdTuple) = chars_split(cmd, " ", 0);

        if (CHARS_EQUAL(cmd, "@")) {
            echoIsEnabled = !echoIsEnabled;
        }

        // Exit
        if (CHARS_EQUAL(cmd, "q")
            || CHARS_EQUAL(cmd, "exit")
            || CHARS_EQUAL(cmd, "quit"))
            break;

        // List of all documents
        if (CHARS_EQUAL(cmd, "l") || CHARS_EQUAL(cmd, "list")) {
            for (size_t i = 0; i < docList->length; ++i) {
                struct DocumentInfo *documentInfo = docList->list[i];
                if (echoIsEnabled) {
                    printf("#%zu %s [%zu] (%s)\n",
                           i + 1,
                           chars_replace(documentInfo->path, homeDir, ""),
                           documentInfo->sectionList->length,
                           documentInfo->title);
                }
            }
        }

        // Print document
        if ((CHARS_EQUAL(cmdTuple->list[0]->list, "print")
             || CHARS_EQUAL(cmdTuple->list[0]->list, "p")) && cmdTuple->length > 1) {
            size_t id = chars_to_int(cmdTuple->list[1]->list) - 1;
            struct DocumentInfo *documentInfo = docList->list[id];

            EQU_BLOB(file) = file_get_contents(documentInfo->path);

            // Search start of file
            for (size_t i = 0; i < documentInfo->sectionList->length; ++i) {
                struct DocumentSection *documentSection = documentInfo->sectionList->list[i];
                if (echoIsEnabled) printf("SECTION %s [%zu:%zu]\n" "\n", documentSection->name,
                       documentSection->start, documentSection->end);
                char *part = chars_substr((char *)file->list, documentSection->start, documentSection->end);
                if (echoIsEnabled) printf("%s\n", part);
                MEMORY_FREE(part)
            }

            DESTROY_BLOB(file)
        }

        if ((CHARS_EQUAL(cmdTuple->list[0]->list, "info")
             || CHARS_EQUAL(cmdTuple->list[0]->list, "i")) && cmdTuple->length > 1) {
            size_t id = chars_to_int(cmdTuple->list[1]->list) - 1;
            struct DocumentInfo *documentInfo = docList->list[id];

            EQU_BLOB(file) = file_get_contents(documentInfo->path);

            // Search start of file
            size_t endOfHeader = 0;
            for (size_t i = 0; i < file->length; ++i) {
                if (file->list[i] == '\n' && file->list[i + 1] == '\n') {
                    endOfHeader = i;
                    break;
                }
            }

            char *header = chars_substr((char *)file->list, 0, endOfHeader);
            if (echoIsEnabled) printf("%s\n", header);
            MEMORY_FREE(header)
            DESTROY_BLOB(file)
        }

        // Search through documents
        if ((CHARS_EQUAL(cmdTuple->list[0]->list, "search")
             || CHARS_EQUAL(cmdTuple->list[0]->list, "s")) && cmdTuple->length > 1) {
            search_by_all(docList, cmdTuple->list[1]->list, SEARCH_BY_ALL);
        }

        // Search and print first result
        if (CHARS_EQUAL(cmdTuple->list[0]->list, "sp") && cmdTuple->length > 1) {
            search_by_all(docList, cmdTuple->list[1]->list, SEARCH_BY_ALL | PRINT_FIRST_RESULT);
        }

        // Search by tag
        if (CHARS_EQUAL(cmdTuple->list[0]->list, "tag") && cmdTuple->length > 1) {
            search_by_all(docList, cmdTuple->list[1]->list, SEARCH_BY_TAGS);
        }

        // Search by section
        if ((CHARS_EQUAL(cmdTuple->list[0]->list, "section")
        || CHARS_EQUAL(cmdTuple->list[0]->list, "sec")) && cmdTuple->length > 1) {
            search_by_all(docList, cmdTuple->list[1]->list, SEARCH_BY_SECTION);
        }

        // Print file section
        if (strncmp(cmd, "ps ", 3) == 0) {
            EQU_STRING_ARRAY(X) = chars_split(cmd + 3, " ", 1);
            size_t id = chars_to_int(X->list[0]->list) - 1;
            struct DocumentInfo *documentInfo = docList->list[id];
            EQU_BLOB(file) = file_get_contents(documentInfo->path);

            for (size_t i = 0; i < documentInfo->sectionList->length; ++i) {
                struct DocumentSection *section = documentInfo->sectionList->list[i];

                // Check tag is matched
                if (chars_match(section->name, X->list[1]->list, REG_ICASE)) {
                    char *sectionData = chars_substr((char *)file->list, section->start, section->end);
                    if (echoIsEnabled) printf("%s\n", sectionData);
                    MEMORY_FREE(sectionData)
                    break;
                }
            }

            DESTROY_BLOB(file)
            DESTROY_STRING_ARRAY(X)
        }

        if ((strcmp(cmd, "clear") == 0 || strcmp(cmd, "c") == 0) && echoIsEnabled) {
            console_fill_screen(' ');
        }

        if ((strcmp(cmd, "help") == 0 || strcmp(cmd, "h") == 0) && echoIsEnabled) {
            puts("l:list - Print all documents");
            puts("p:print %id - Print document");
            puts("i:info %id - Print info about document");
            puts("s:search %name - Search by all params");
            puts("tag %name - Search by tag");
            puts("sec:section %name - Search by section");
            puts("ps %id %section - Print section of document");
            puts("c:clear - Clear screen");
            puts("q:exit - Exit");
        }

        DESTROY_STRING_ARRAY(cmdTuple)

        if (!isInteractiveMode && !lastStatus) break;
    }

    return 0;
}
