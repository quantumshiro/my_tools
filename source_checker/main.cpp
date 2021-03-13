#include <iostream>
#include <fstream>
#include <cassert>
#include <cstdlib>

/* check the encoding of one file */
bool checkOneFile(const char *filename);

/* check the encoding of files */
int main(int argc, const char *argv[])
{
    /* loop through the arguments */
    for (int i = 1; i < argc; i++) {
        bool res = checkOneFile(argv[i]);
        if (! res) {
            /* failure (following linux convention) */
            return EXIT_FAILURE;
        }
    }
    /* success (following linux convention) */
    return EXIT_SUCCESS;
}

/* states for the state machine to check source code bytes */
enum State {
    S_NORMAL,   /* during a line, after a regular character */
    S_BOL,      /* beginning of line */
    S_CR,       /* after a CR */
    S_UTF_EXPECT3,  /* expecting 3 more UTF8 trailers to follow */
    S_UTF_EXPECT2,  /* expecting 2 more */
    S_UTF_EXPECT1,  /* expecting 1 more */
};

void report(int incident_count, const char *msg, const char *filename, int line_count) {
    /* report the incident just once per file. */
    if (incident_count == 1) {
        std::cout << filename << "(" << (line_count+1) << ") [ERROR] :" << msg << std::endl;
    }
}

bool isUtf8Trailer(char c) {
    // check if the byte has the bits 10xx xxxx
    return (c & 0xc0) == 0x80;
    // 0xc0 is "C0" in hexadecimal. C is 12. (0123456789ABCDEF are the digits)
    // 12 is 2^3 + 2^2. in binary it is 1100
    // 0xc0 in binary is 11000000
    // 0x80 in binary is 10000000
}

bool isUtf8Header1(char c) {
    // 110x xxxx
    return (c & 0xe0) == 0xc0;
}

bool isUtf8Header2(char c) {
    // 1110 xxxx
    return (c & 0xf0) == 0xe0;
}

bool isUtf8Header3(char c) {
    // 1111 0xxx
    return (c & 0xf8) == 0xf0;
}

bool checkOneFile(const char *filename)
{
    std::cout << "Checking " << filename << std::endl;
    std::ifstream ifs(filename, std::ios::binary);
    if (! ifs.is_open()) {
        /* standard C function to print an error message */
        perror(filename);
        return false;
    }
    /* number of End Of Line(EOL) characters we have seen */
    int line_count = 0;
    /* number of tab characters (banned by our coding convention). */
    int tab_count = 0;
    /* number of CR-LF sequences (banned) */
    int crlf_count = 0;
    /* number of CR occurences (banned) */
    int cr_count = 0;
    /* number of other C1 segment control characters (0x01 through 0x1f) */
    /* They do not appear in normal text files */
    int c1_count = 0;
    /* number of bad UTF-8 sequences. Happens when character code setting of the editor is incorrect */
    int bad_utf_count = 0;

    /* state variable for the state machine */
    State ss = S_BOL;

    /*
     * when an ifstream class object is referenced in an if condition,
     * ifstream will be casted into a bool type by a custom-made
     * cast function.  The bool value 'true' will mean that the
     * ifstream object is in good state. False will mean that no more
     * data can be obtained from the stream.
     */
    while (ifs) {
        char c; // note that this is a 'signed' type and the range is -128 to +127
        // read one byte from the stream.
        ifs.get(c);

        // reenter point, when state transition happens without
        // consuming the input character
        reenter_point:

        switch (ss) {
            case S_BOL:
                ss = S_NORMAL;
                /* fall through */
            case S_NORMAL:
            {
                switch(c) {
                    case '\n':
                        line_count++;
                        ss = S_BOL;
                        break;
                    case '\r':
                        ss = S_CR;
                        break;
                    case '\t':
                        tab_count++;
                        report(tab_count, "Tab character", filename, line_count);
                        break;
                    default:
                        if (isUtf8Header3(c)) {
                            ss = S_UTF_EXPECT3;
                        } else if (isUtf8Header2(c)) {
                            ss = S_UTF_EXPECT2;
                        } else if (isUtf8Header1(c)) {
                            ss = S_UTF_EXPECT1;
                        } else if (isUtf8Trailer(c)) {
                            bad_utf_count++;
                            report(bad_utf_count, "Bad multibyte sequence", filename, line_count);
                        } else if (((unsigned char)c) < 0x20) {
                            c1_count++;
                            report(c1_count, "Unexpected control character", filename, line_count);
                        }
                        break;
                }
            }
            break;
            case S_CR:
            {
                if (c == '\n') {
                    // CRLF = Windows EOL code
                    crlf_count++;
                    report(crlf_count, "Windows newline sequence (CR,LF)", filename, line_count);
                    line_count++;
                    ss = S_BOL;
                } else {
                    // CR = Very Old MacOS EOL code
                    cr_count++;
                    report(cr_count, "Old-time MacOS newline sequence (CR)", filename, line_count);
                    line_count++;
                    ss = S_BOL;
                    /* We do not consume the current cc. */
                    goto reenter_point;
                }
            }
            break;
            case S_UTF_EXPECT3:
            {
                if (isUtf8Trailer(c)) {
                    // correct UTF-8 sequence.
                    ss = S_UTF_EXPECT2;
                } else {
                    bad_utf_count++;
                    report(bad_utf_count, "Bad multibyte sequence", filename, line_count);
                    ss = S_NORMAL;
                }
            }
            break;
            case S_UTF_EXPECT2:
            {
                if (isUtf8Trailer(c)) {
                    // correct UTF-8 sequence.
                    ss = S_UTF_EXPECT1;
                } else {
                    bad_utf_count++;
                    report(bad_utf_count, "Bad multibyte sequence", filename, line_count);
                    ss = S_NORMAL;
                }
            }
            break;
            case S_UTF_EXPECT1:
            {
                if (isUtf8Trailer(c)) {
                    // correct UTF-8 sequence.
                    ss = S_NORMAL;
                } else {
                    bad_utf_count++;
                    report(bad_utf_count, "Bad multibyte sequence", filename, line_count);
                    ss = S_NORMAL;
                }
            }
            break;
            default:
                assert(0);
        }
    }
    if (ss != S_BOL) {
        report(1, "Missing EOL at end of file", filename, line_count);
    }
    /*
     * the ifs object should be in eof (end of file) state now.
     * If it isn't, that means some other error happened.
     */
    if (! ifs.eof()) {
        perror(filename);
        return false;
    }
    ifs.close();
    return true;
}