#define WIN32_LEAN_AND_MEAN

#include <ctype.h>
#include <conio.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#define ACCOUNT_FILE "accounts.txt"
#define TRANSACTION_FILE "transactions.txt"
#define ACCOUNT_TEMP_FILE "accounts.tmp"

#define MAX_USERS 100
#define NAME_SIZE 50
#define PIN_SIZE 5
#define ACCOUNT_TYPE_SIZE 20
#define ACCOUNT_NUMBER_SIZE 30
#define LINE_SIZE 512

#define PIN_LENGTH 4
#define SESSION_START_SECONDS (19 * 60 + 59)
#define MAX_TRANSACTION_AMOUNT 1000000000.0
#define MAX_ACCOUNT_BALANCE 1000000000000.0

#define HEADER_WIDTH 60
#define HEADER_TIMER_INNER_COLUMN 47
#define HEADER_TIMER_ROW 1
#define TIMER_TEXT_LENGTH 5

typedef struct {
    char name[NAME_SIZE];
    char pin[PIN_SIZE];
    char accountType[ACCOUNT_TYPE_SIZE];
    char accountNumber[ACCOUNT_NUMBER_SIZE];
    double balance;
} Account;

typedef enum {
    INPUT_ANY,
    INPUT_DIGITS,
    INPUT_AMOUNT,
    INPUT_ACCOUNT_NUMBER,
    INPUT_YES_NO
} InputMode;

Account users[MAX_USERS];
int totalUsers = 0;
int currentUserIndex = -1;
Account currentAccount;

int invalidRecordCount = 0;
int duplicateRecordCount = 0;
int accountsFileWasMissing = 0;

int sessionActive = 0;
int sessionExpired = 0;
int timerVisible = 0;
int timerPositionKnown = 0;
int timerRefreshLocked = 0;
int lastDisplayedTimer = -1;
time_t sessionExpiresAt = 0;
COORD timerCursorPosition = {0, 0};

void setColor(int color);
void resetColor(void);
void clearScreen(void);
void flushOutput(void);
void configureConsole(void);

void playSuccessSound(void);
void playErrorSound(void);
void playWarningSound(void);

void trimWhitespace(char *text);
void removeNewline(char *text);
int isDigitsExact(const char *text, int length);
int isValidAccountNumber(const char *accountNumber);
int splitFields(char *line, char *fields[], int expectedFields);
int parseMoney(const char *text, double *amount, int allowZero, double maxValue);

void loadAccounts(void);
int saveAccounts(void);
int findAccountByNumber(const char *accountNumber);
int findAccountByPin(const char *pin);
int syncCurrentAccount(void);

void startSessionTimer(void);
int getRemainingSeconds(void);
void formatTimer(char output[6]);
int gotoxy(short x, short y);
void captureHeaderTimerPosition(void);
void updateTimerTextOnly(int force);
int checkSessionActive(void);
void expireSession(void);
int sleepResponsive(DWORD milliseconds);

void header(void);
void footer(void);
int pauseScreen(void);
int readTimedLine(const char *prompt, char *buffer, size_t bufferSize,
                  InputMode mode, int masked, size_t maxChars);
int askYesNo(const char *prompt, int *answer);

void typeWriter(const char *text);
int loadingBar(const char *message);
void bootScreen(void);
void insertCardAnimation(void);

int loginSystem(void);
void generateTransactionId(char transactionId[], size_t size);
int saveTransactionForAccount(const Account *account, const char *transactionId,
                              const char *timestamp,
                              const char *transactionType, double amount,
                              const char *note);
int generateReceipt(const char *transactionId, const char *timestamp,
                    const char *transactionType, double amount,
                    char receiptFileName[], size_t receiptFileNameSize);

int checkBalance(void);
int depositMoney(void);
int withdrawMoney(void);
int transferFunds(void);
int transactionHistory(void);
int changePin(void);
int menu(void);

void setColor(int color) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), (WORD)color);
}

void resetColor(void) {
    setColor(15);
}

void flushOutput(void) {
    fflush(stdout);
}

void configureConsole(void) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (output != INVALID_HANDLE_VALUE && GetConsoleMode(output, &mode)) {
        mode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(output, mode);
    }

    srand((unsigned int)(time(NULL) ^ GetCurrentProcessId()));
}

void clearScreen(void) {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    DWORD cells;
    DWORD written;
    COORD home = {0, 0};
    SHORT windowHeight;

    timerVisible = 0;
    timerPositionKnown = 0;
    lastDisplayedTimer = -1;

    if (output == INVALID_HANDLE_VALUE ||
        !GetConsoleScreenBufferInfo(output, &info)) {
        system("cls");
        flushOutput();
        return;
    }

    home.Y = info.srWindow.Top;
    windowHeight = (SHORT)(info.srWindow.Bottom - info.srWindow.Top + 1);
    cells = (DWORD)info.dwSize.X * (DWORD)windowHeight;
    FillConsoleOutputCharacterA(output, ' ', cells, home, &written);
    FillConsoleOutputAttribute(output, info.wAttributes, cells, home, &written);
    SetConsoleCursorPosition(output, home);
    flushOutput();
}

void playTone(DWORD frequency, DWORD duration) {
    if (!Beep(frequency, duration)) {
        MessageBeep(MB_OK);
    }
}

void playSuccessSound(void) {
    playTone(880, 90);
    Sleep(35);
    playTone(1175, 110);
}

void playErrorSound(void) {
    playTone(420, 180);
    Sleep(40);
    playTone(320, 240);
}

void playWarningSound(void) {
    playTone(700, 90);
}

void removeNewline(char *text) {
    text[strcspn(text, "\r\n")] = '\0';
}

void trimWhitespace(char *text) {
    char *start = text;
    char *end;

    while (*start && isspace((unsigned char)*start)) {
        start++;
    }

    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    if (*text == '\0') {
        return;
    }

    end = text + strlen(text) - 1;
    while (end >= text && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
}

int isDigitsExact(const char *text, int length) {
    int i;

    if ((int)strlen(text) != length) {
        return 0;
    }

    for (i = 0; i < length; i++) {
        if (!isdigit((unsigned char)text[i])) {
            return 0;
        }
    }

    return 1;
}

int isValidAccountNumber(const char *accountNumber) {
    size_t i;
    size_t length = strlen(accountNumber);

    if (length == 0 || length >= ACCOUNT_NUMBER_SIZE) {
        return 0;
    }

    for (i = 0; i < length; i++) {
        if (!isalnum((unsigned char)accountNumber[i])) {
            return 0;
        }
    }

    return 1;
}

int splitFields(char *line, char *fields[], int expectedFields) {
    int i;
    char *cursor = line;
    char *separator;

    for (i = 0; i < expectedFields; i++) {
        fields[i] = cursor;
        separator = strchr(cursor, '|');

        if (i < expectedFields - 1) {
            if (separator == NULL) {
                return 0;
            }
            *separator = '\0';
            cursor = separator + 1;
        } else if (separator != NULL) {
            return 0;
        }
    }

    return 1;
}

int parseMoney(const char *text, double *amount, int allowZero, double maxValue) {
    char *end = NULL;
    double value;

    if (text == NULL || *text == '\0') {
        return 0;
    }

    if (text[0] == '-' || text[0] == '+') {
        return 0;
    }

    errno = 0;
    value = strtod(text, &end);

    if (text == end || errno == ERANGE) {
        return 0;
    }

    while (end != NULL && *end != '\0') {
        if (!isspace((unsigned char)*end)) {
            return 0;
        }
        end++;
    }

    if ((!allowZero && value <= 0.0) || (allowZero && value < 0.0)) {
        return 0;
    }

    if (value > maxValue) {
        return 0;
    }

    *amount = value;
    return 1;
}

int accountNumberAlreadyLoaded(const char *accountNumber) {
    int i;

    for (i = 0; i < totalUsers; i++) {
        if (strcmp(users[i].accountNumber, accountNumber) == 0) {
            return 1;
        }
    }

    return 0;
}

int parseAccountRecord(char *line, Account *account) {
    char *fields[5];
    double balance;

    removeNewline(line);

    if (!splitFields(line, fields, 5)) {
        return 0;
    }

    trimWhitespace(fields[0]);
    trimWhitespace(fields[1]);
    trimWhitespace(fields[2]);
    trimWhitespace(fields[3]);
    trimWhitespace(fields[4]);

    if (fields[0][0] == '\0' || strlen(fields[0]) >= NAME_SIZE) {
        return 0;
    }

    if (!isDigitsExact(fields[1], PIN_LENGTH)) {
        return 0;
    }

    if (fields[2][0] == '\0' || strlen(fields[2]) >= ACCOUNT_TYPE_SIZE) {
        return 0;
    }

    if (!isValidAccountNumber(fields[3])) {
        return 0;
    }

    if (!parseMoney(fields[4], &balance, 1, MAX_ACCOUNT_BALANCE)) {
        return 0;
    }

    strncpy(account->name, fields[0], NAME_SIZE - 1);
    account->name[NAME_SIZE - 1] = '\0';
    strncpy(account->pin, fields[1], PIN_SIZE - 1);
    account->pin[PIN_SIZE - 1] = '\0';
    strncpy(account->accountType, fields[2], ACCOUNT_TYPE_SIZE - 1);
    account->accountType[ACCOUNT_TYPE_SIZE - 1] = '\0';
    strncpy(account->accountNumber, fields[3], ACCOUNT_NUMBER_SIZE - 1);
    account->accountNumber[ACCOUNT_NUMBER_SIZE - 1] = '\0';
    account->balance = balance;

    return 1;
}

void loadAccounts(void) {
    FILE *file;
    char line[LINE_SIZE];
    Account parsedAccount;

    totalUsers = 0;
    invalidRecordCount = 0;
    duplicateRecordCount = 0;
    accountsFileWasMissing = 0;

    file = fopen(ACCOUNT_FILE, "r");
    if (file == NULL) {
        FILE *createdFile;

        accountsFileWasMissing = 1;
        createdFile = fopen(ACCOUNT_FILE, "w");
        if (createdFile != NULL) {
            fclose(createdFile);
        }
        return;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char originalLine[LINE_SIZE];

        strncpy(originalLine, line, sizeof(originalLine) - 1);
        originalLine[sizeof(originalLine) - 1] = '\0';

        trimWhitespace(originalLine);
        if (originalLine[0] == '\0') {
            continue;
        }

        if (!parseAccountRecord(line, &parsedAccount)) {
            invalidRecordCount++;
            continue;
        }

        if (accountNumberAlreadyLoaded(parsedAccount.accountNumber)) {
            duplicateRecordCount++;
            continue;
        }

        if (totalUsers >= MAX_USERS) {
            invalidRecordCount++;
            continue;
        }

        users[totalUsers] = parsedAccount;
        totalUsers++;
    }

    fclose(file);
}

int saveAccounts(void) {
    FILE *file = fopen(ACCOUNT_TEMP_FILE, "w");
    int i;

    if (file == NULL) {
        return 0;
    }

    for (i = 0; i < totalUsers; i++) {
        fprintf(file, "%s|%s|%s|%s|%.2f\n",
                users[i].name,
                users[i].pin,
                users[i].accountType,
                users[i].accountNumber,
                users[i].balance);
    }

    if (fclose(file) != 0) {
        remove(ACCOUNT_TEMP_FILE);
        return 0;
    }

    if (!MoveFileExA(ACCOUNT_TEMP_FILE, ACCOUNT_FILE,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        remove(ACCOUNT_FILE);
        if (rename(ACCOUNT_TEMP_FILE, ACCOUNT_FILE) != 0) {
            return 0;
        }
    }

    return 1;
}

int findAccountByNumber(const char *accountNumber) {
    int i;

    for (i = 0; i < totalUsers; i++) {
        if (strcmp(users[i].accountNumber, accountNumber) == 0) {
            return i;
        }
    }

    return -1;
}

int findAccountByPin(const char *pin) {
    int i;

    for (i = 0; i < totalUsers; i++) {
        if (strcmp(users[i].pin, pin) == 0) {
            return i;
        }
    }

    return -1;
}

int syncCurrentAccount(void) {
    if (currentUserIndex < 0 || currentUserIndex >= totalUsers) {
        return 0;
    }

    users[currentUserIndex] = currentAccount;
    return 1;
}

void startSessionTimer(void) {
    sessionActive = 1;
    sessionExpired = 0;
    sessionExpiresAt = time(NULL) + SESSION_START_SECONDS;
    lastDisplayedTimer = -1;
}

int getRemainingSeconds(void) {
    long remaining;

    if (!sessionActive) {
        return 0;
    }

    remaining = (long)difftime(sessionExpiresAt, time(NULL));
    if (remaining < 0) {
        remaining = 0;
    }

    if (remaining > SESSION_START_SECONDS) {
        remaining = SESSION_START_SECONDS;
    }

    return (int)remaining;
}

void formatTimer(char output[6]) {
    int remaining = getRemainingSeconds();

    snprintf(output, 6, "%02d:%02d", remaining / 60, remaining % 60);
}

int gotoxy(short x, short y) {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD position;

    if (output == INVALID_HANDLE_VALUE) {
        return 0;
    }

    position.X = x;
    position.Y = y;
    return SetConsoleCursorPosition(output, position) != 0;
}

void captureHeaderTimerPosition(void) {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;

    timerPositionKnown = 0;

    if (output == INVALID_HANDLE_VALUE ||
        !GetConsoleScreenBufferInfo(output, &info)) {
        return;
    }

    timerCursorPosition.X =
        (SHORT)(info.dwCursorPosition.X + 1 + HEADER_TIMER_INNER_COLUMN);
    timerCursorPosition.Y =
        (SHORT)(info.dwCursorPosition.Y + HEADER_TIMER_ROW);
    timerPositionKnown = 1;
}

void updateTimerTextOnly(int force) {
    HANDLE output;
    CONSOLE_SCREEN_BUFFER_INFO info;
    COORD originalPosition;
    WORD originalAttributes;
    char timerText[6];
    int remaining;

    if (!sessionActive || !timerVisible || !timerPositionKnown ||
        timerRefreshLocked) {
        return;
    }

    remaining = getRemainingSeconds();
    if (!force && remaining == lastDisplayedTimer) {
        return;
    }

    output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == INVALID_HANDLE_VALUE ||
        !GetConsoleScreenBufferInfo(output, &info)) {
        return;
    }

    lastDisplayedTimer = remaining;
    originalPosition = info.dwCursorPosition;
    originalAttributes = info.wAttributes;

    formatTimer(timerText);
    if (!gotoxy(timerCursorPosition.X, timerCursorPosition.Y)) {
        SetConsoleTextAttribute(output, originalAttributes);
        return;
    }

    setColor(14);
    printf("%-*s", TIMER_TEXT_LENGTH, timerText);
    SetConsoleTextAttribute(output, originalAttributes);
    SetConsoleCursorPosition(output, originalPosition);
    flushOutput();
}

void expireSession(void) {
    if (sessionExpired) {
        return;
    }

    updateTimerTextOnly(1);

    sessionExpired = 1;
    sessionActive = 0;
    currentUserIndex = -1;
    memset(&currentAccount, 0, sizeof(currentAccount));

    clearScreen();
    setColor(12);
    printf("\n\n");
    printf("                 SESSION EXPIRED\n\n");
    setColor(14);
    printf("           Returning to Welcome Screen...\n");
    resetColor();
    flushOutput();
    playWarningSound();
    Sleep(2200);
}

int checkSessionActive(void) {
    if (!sessionActive) {
        return 1;
    }

    updateTimerTextOnly(0);

    if (time(NULL) >= sessionExpiresAt) {
        updateTimerTextOnly(1);
        expireSession();
        return 0;
    }

    return 1;
}

int sleepResponsive(DWORD milliseconds) {
    DWORD elapsed = 0;

    while (elapsed < milliseconds) {
        DWORD step = milliseconds - elapsed;
        if (step > 40) {
            step = 40;
        }

        Sleep(step);
        elapsed += step;

        if (!checkSessionActive()) {
            return 0;
        }
    }

    return 1;
}

void printRepeated(const char *text, int count) {
    int i;

    for (i = 0; i < count; i++) {
        printf("%s", text);
    }
}

void header(void) {
    char timerText[6] = "19:59";
    const char *title = " SECURE BANK TERMINAL";
    int titleLength = (int)strlen(title);
    int spacesBeforeTimer = HEADER_TIMER_INNER_COLUMN - titleLength;
    int spacesAfterTimer = HEADER_WIDTH - HEADER_TIMER_INNER_COLUMN - 5;

    timerRefreshLocked = 1;
    clearScreen();
    captureHeaderTimerPosition();
    formatTimer(timerText);

    setColor(11);
    printf("╔");
    printRepeated("═", HEADER_WIDTH);
    printf("╗\n");
    printf("║%s%*s%s%*s║\n", title, spacesBeforeTimer, "",
           timerText, spacesAfterTimer, "");
    printf("╚");
    printRepeated("═", HEADER_WIDTH);
    printf("╝\n");

    setColor(13);
    printf("\nATM SIMULATOR SYSTEM\n\n");

    setColor(10);
    printf("USER    : %s\n", currentAccount.name);
    setColor(15);
    printf("ACCOUNT : %s\n", currentAccount.accountType);
    setColor(14);
    printf("STATUS  : ACTIVE\n");

    setColor(11);
    printRepeated("═", HEADER_WIDTH + 2);
    printf("\n\n");
    resetColor();
    flushOutput();

    timerVisible = 1;
    lastDisplayedTimer = getRemainingSeconds();
    timerRefreshLocked = 0;
}

void footer(void) {
    setColor(8);
    printf("\n");
    printRepeated("═", HEADER_WIDTH + 2);
    printf("\n");
    setColor(10);
    printf("SECURE CONNECTION ACTIVE | RBI VERIFIED NETWORK\n");
    setColor(8);
    printRepeated("═", HEADER_WIDTH + 2);
    printf("\n");
    resetColor();
    flushOutput();
}

int pauseScreen(void) {
    int key;

    setColor(8);
    printf("\nPress ENTER to continue...");
    resetColor();
    flushOutput();

    while (1) {
        if (!checkSessionActive()) {
            return 0;
        }

        if (_kbhit()) {
            key = _getch();
            if (key == 0 || key == 224) {
                if (_kbhit()) {
                    _getch();
                }
                continue;
            }

            if (key == '\r') {
                printf("\n");
                flushOutput();
                return 1;
            }
        }

        Sleep(25);
    }
}

int inputCharAllowed(int key, InputMode mode, const char *currentBuffer) {
    switch (mode) {
        case INPUT_DIGITS:
            return isdigit((unsigned char)key);

        case INPUT_AMOUNT:
            if (isdigit((unsigned char)key)) {
                return 1;
            }
            return key == '.' && strchr(currentBuffer, '.') == NULL;

        case INPUT_ACCOUNT_NUMBER:
            return isalnum((unsigned char)key);

        case INPUT_YES_NO:
            return (currentBuffer[0] == '\0' &&
                    (key == 'Y' || key == 'y' || key == 'N' || key == 'n'));

        case INPUT_ANY:
        default:
            return isprint((unsigned char)key);
    }
}

int readTimedLine(const char *prompt, char *buffer, size_t bufferSize,
                  InputMode mode, int masked, size_t maxChars) {
    size_t length = 0;

    if (bufferSize == 0) {
        return 0;
    }

    if (maxChars == 0 || maxChars >= bufferSize) {
        maxChars = bufferSize - 1;
    }

    buffer[0] = '\0';
    printf("%s", prompt);
    flushOutput();

    while (1) {
        int key;

        if (!checkSessionActive()) {
            return 0;
        }

        if (!_kbhit()) {
            Sleep(25);
            continue;
        }

        key = _getch();
        if (key == 0 || key == 224) {
            if (_kbhit()) {
                _getch();
            }
            continue;
        }

        if (key == '\r') {
            buffer[length] = '\0';
            printf("\n");
            flushOutput();
            return 1;
        }

        if (key == '\b') {
            if (length > 0) {
                length--;
                buffer[length] = '\0';
                printf("\b \b");
                flushOutput();
            }
            continue;
        }

        if (key == 27) {
            continue;
        }

        if (!inputCharAllowed(key, mode, buffer)) {
            playWarningSound();
            continue;
        }

        if (length >= maxChars) {
            playWarningSound();
            continue;
        }

        buffer[length++] = (char)key;
        buffer[length] = '\0';

        putchar(masked ? '*' : key);
        flushOutput();
    }
}

int askYesNo(const char *prompt, int *answer) {
    char response[4];

    while (1) {
        if (!readTimedLine(prompt, response, sizeof(response), INPUT_YES_NO, 0, 1)) {
            return 0;
        }

        if (response[0] == 'Y' || response[0] == 'y') {
            *answer = 1;
            return 1;
        }

        if (response[0] == 'N' || response[0] == 'n') {
            *answer = 0;
            return 1;
        }

        setColor(12);
        printf("Please enter Y or N.\n");
        resetColor();
    }
}

void typeWriter(const char *text) {
    int i;

    for (i = 0; text[i] != '\0'; i++) {
        putchar(text[i]);
        flushOutput();
        if (!sleepResponsive(18)) {
            return;
        }
    }
}

int loadingBar(const char *message) {
    int i;

    setColor(11);
    printf("\n%s\n\n", message);
    printf("[");
    flushOutput();

    for (i = 0; i < 30; i++) {
        if (!sleepResponsive(35)) {
            return 0;
        }
        setColor(10);
        printf("█");
        flushOutput();
    }

    resetColor();
    printf("] 100%%\n");
    flushOutput();

    return sleepResponsive(420);
}

void bootScreen(void) {
    clearScreen();

    setColor(10);
    typeWriter("INITIALIZING SECURE BANKING SERVICES...\n");
    sleepResponsive(350);
    typeWriter("CONNECTING TO RBI SECURE NETWORK...\n");
    sleepResponsive(350);
    typeWriter("VERIFYING ENCRYPTION KEYS...\n");
    sleepResponsive(350);
    typeWriter("SECURITY NODE VERIFIED...\n");
    sleepResponsive(350);
    loadingBar("LOADING TERMINAL");

    setColor(10);
    printf("\nACCESS TO SECURE TERMINAL GRANTED\n");
    resetColor();
    flushOutput();
    sleepResponsive(900);
}

void drawCardFrame(const char *status, int color) {
    clearScreen();
    setColor(11);
    printf("\n\n");
    printf("╔════════════════════════════════════════════════╗\n");
    printf("║                  ATM CARD SLOT                ║\n");
    printf("╚════════════════════════════════════════════════╝\n\n");
    setColor(color);
    printf("                    %s\n", status);
    resetColor();
    flushOutput();
}

void animatedCardStatus(const char *status, int color, int dots) {
    int i;

    drawCardFrame(status, color);
    if (dots) {
        printf("\n                    ");
        for (i = 0; i < 5; i++) {
            printf(".");
            flushOutput();
            sleepResponsive(230);
        }
        printf("\n");
    }
    flushOutput();
    sleepResponsive(450);
}

void insertCardAnimation(void) {
    drawCardFrame("INSERT ATM CARD", 14);
    sleepResponsive(800);

    setColor(8);
    printf("\n                       ↓\n");
    resetColor();
    flushOutput();
    sleepResponsive(350);

    animatedCardStatus("CARD INSERTED", 10, 0);
    animatedCardStatus("READING CHIP", 11, 1);
    animatedCardStatus("VERIFYING CARD", 13, 1);
    animatedCardStatus("CONNECTING TO BANK SERVER", 11, 1);
    animatedCardStatus("AUTHENTICATION SUCCESSFUL", 10, 0);
    playSuccessSound();
    sleepResponsive(650);
    clearScreen();
}

int loginSystem(void) {
    int attempts = 0;

    while (attempts < 3) {
        char enteredPin[PIN_SIZE];
        int userIndex;

        clearScreen();
        setColor(11);
        printf("╔══════════════════════════════════════════════════╗\n");
        printf("║              SECURE BANK TERMINAL               ║\n");
        printf("╚══════════════════════════════════════════════════╝\n");

        setColor(13);
        printf("               ATM SIMULATOR SYSTEM\n\n");
        setColor(14);
        printf("SECURE LOGIN PORTAL\n\n");
        resetColor();

        if (accountsFileWasMissing) {
            setColor(14);
            printf("accounts.txt was missing, so a new empty file was created.\n");
            resetColor();
        }

        if (invalidRecordCount > 0 || duplicateRecordCount > 0) {
            setColor(14);
            printf("Skipped %d corrupted record(s) and %d duplicate account(s).\n\n",
                   invalidRecordCount, duplicateRecordCount);
            resetColor();
        }

        if (totalUsers == 0) {
            setColor(12);
            printf("No valid accounts are available in %s.\n", ACCOUNT_FILE);
            resetColor();
            pauseScreen();
            return 0;
        }

        if (!readTimedLine("Enter ATM PIN : ", enteredPin, sizeof(enteredPin),
                           INPUT_DIGITS, 1, PIN_LENGTH)) {
            return 0;
        }

        if (!isDigitsExact(enteredPin, PIN_LENGTH)) {
            attempts++;
            setColor(12);
            printf("\nPIN must contain exactly %d digits.\n", PIN_LENGTH);
            printf("Attempts Remaining : %d\n", 3 - attempts);
            resetColor();
            playErrorSound();
            sleepResponsive(1200);
            continue;
        }

        userIndex = findAccountByPin(enteredPin);
        if (userIndex >= 0) {
            currentUserIndex = userIndex;
            currentAccount = users[userIndex];
            startSessionTimer();

            header();
            setColor(10);
            printf("ACCESS GRANTED\n");
            resetColor();
            playSuccessSound();
            if (!loadingBar("RETRIEVING ACCOUNT DATA")) {
                return 0;
            }
            return 1;
        }

        attempts++;
        setColor(12);
        printf("\nINVALID PIN!\n");
        printf("Attempts Remaining : %d\n", 3 - attempts);
        resetColor();
        playErrorSound();
        sleepResponsive(1300);
    }

    clearScreen();
    setColor(12);
    printf("\nACCOUNT LOCKED!\n");
    printf("PLEASE CONTACT YOUR BANK.\n");
    resetColor();
    playErrorSound();
    sleepResponsive(2000);
    return 0;
}

void getTimestamp(char *timestamp, size_t size) {
    time_t now = time(NULL);
    struct tm *timeInfo = localtime(&now);

    if (timeInfo == NULL) {
        strncpy(timestamp, "0000-00-00 00:00:00", size - 1);
        timestamp[size - 1] = '\0';
        return;
    }

    strftime(timestamp, size, "%Y-%m-%d %H:%M:%S", timeInfo);
}

int transactionIdExists(const char *transactionId) {
    FILE *file = fopen(TRANSACTION_FILE, "r");
    char line[LINE_SIZE];
    size_t idLength = strlen(transactionId);

    if (file == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, transactionId, idLength) == 0 &&
            line[idLength] == '|') {
            fclose(file);
            return 1;
        }
    }

    fclose(file);
    return 0;
}

void generateTransactionId(char transactionId[], size_t size) {
    int attempts;

    for (attempts = 0; attempts < 50; attempts++) {
        unsigned long value =
            ((unsigned long)time(NULL) * 1000UL +
             (unsigned long)GetTickCount() +
             (unsigned long)clock() +
             (unsigned long)(rand() % 100000)) %
            100000000UL;

        snprintf(transactionId, size, "TXN%08lu", value);
        if (!transactionIdExists(transactionId)) {
            return;
        }
    }

    snprintf(transactionId, size, "TXN%08lu",
             (unsigned long)(rand() % 100000000UL));
}

int saveTransactionForAccount(const Account *account, const char *transactionId,
                              const char *timestamp,
                              const char *transactionType, double amount,
                              const char *note) {
    FILE *file = fopen(TRANSACTION_FILE, "a");

    if (file == NULL) {
        return 0;
    }

    fprintf(file, "%s|%s|%s|%s|%s|%s|%.2f|%.2f|%s\n",
            transactionId,
            timestamp,
            account->accountNumber,
            account->name,
            account->accountType,
            transactionType,
            amount,
            account->balance,
            note == NULL ? "" : note);

    fclose(file);
    return 1;
}

int fileExists(const char *fileName) {
    DWORD attributes = GetFileAttributesA(fileName);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

void buildReceiptFileName(char fileName[], size_t size) {
    time_t now = time(NULL);
    struct tm *timeInfo = localtime(&now);
    char baseName[80];
    int suffix;

    if (timeInfo == NULL) {
        snprintf(baseName, sizeof(baseName), "receipt_%lu",
                 (unsigned long)now);
    } else {
        strftime(baseName, sizeof(baseName), "receipt_%Y%m%d_%H%M%S",
                 timeInfo);
    }

    snprintf(fileName, size, "%s.txt", baseName);
    if (!fileExists(fileName)) {
        return;
    }

    for (suffix = 1; suffix < 1000; suffix++) {
        snprintf(fileName, size, "%s_%03d.txt", baseName, suffix);
        if (!fileExists(fileName)) {
            return;
        }
    }
}

int generateReceipt(const char *transactionId, const char *timestamp,
                    const char *transactionType, double amount,
                    char receiptFileName[], size_t receiptFileNameSize) {
    FILE *receipt;

    buildReceiptFileName(receiptFileName, receiptFileNameSize);
    receipt = fopen(receiptFileName, "w");

    if (receipt == NULL) {
        return 0;
    }

    fprintf(receipt, "====================================\n");
    fprintf(receipt, "       ATM TRANSACTION RECEIPT\n");
    fprintf(receipt, "====================================\n\n");
    fprintf(receipt, "TRANSACTION ID : %s\n", transactionId);
    fprintf(receipt, "TIMESTAMP      : %s\n", timestamp);
    fprintf(receipt, "USER NAME      : %s\n", currentAccount.name);
    fprintf(receipt, "ACCOUNT NUMBER : %s\n", currentAccount.accountNumber);
    fprintf(receipt, "ACCOUNT TYPE   : %s\n", currentAccount.accountType);
    fprintf(receipt, "TRANSACTION    : %s\n", transactionType);
    fprintf(receipt, "AMOUNT         : Rs. %.2f\n", amount);
    fprintf(receipt, "AVAILABLE BAL  : Rs. %.2f\n", currentAccount.balance);
    fprintf(receipt, "\n====================================\n");

    fclose(receipt);
    return 1;
}

void printOperationFailure(const char *message) {
    setColor(12);
    printf("\n%s\n", message);
    resetColor();
    playErrorSound();
}

int checkBalance(void) {
    header();

    setColor(11);
    printf("╔════════════════════════════════════════╗\n");
    setColor(14);
    printf("║           AVAILABLE BALANCE           ║\n");
    setColor(11);
    printf("╠════════════════════════════════════════╣\n");
    setColor(10);
    printf("║          Rs. %-18.2f ║\n", currentAccount.balance);
    setColor(11);
    printf("╚════════════════════════════════════════╝\n");
    resetColor();

    footer();
    return pauseScreen();
}

int depositMoney(void) {
    char amountText[32];
    char transactionId[16];
    char timestamp[32];
    char receiptFileName[128];
    double amount;

    header();
    setColor(10);
    printf("DEPOSIT MONEY\n\n");
    resetColor();

    if (!readTimedLine("Enter Amount : Rs. ", amountText, sizeof(amountText),
                       INPUT_AMOUNT, 0, 18)) {
        return 0;
    }

    if (!parseMoney(amountText, &amount, 0, MAX_TRANSACTION_AMOUNT)) {
        printOperationFailure("INVALID AMOUNT!");
        footer();
        return pauseScreen();
    }

    if (!loadingBar("PROCESSING DEPOSIT")) {
        return 0;
    }

    currentAccount.balance += amount;
    if (!syncCurrentAccount() || !saveAccounts()) {
        currentAccount.balance -= amount;
        syncCurrentAccount();
        printOperationFailure("ACCOUNT DATABASE UPDATE FAILED!");
        footer();
        return pauseScreen();
    }

    generateTransactionId(transactionId, sizeof(transactionId));
    getTimestamp(timestamp, sizeof(timestamp));
    saveTransactionForAccount(&currentAccount, transactionId, timestamp,
                              "DEPOSIT", amount, "Cash deposit");
    generateReceipt(transactionId, timestamp, "DEPOSIT", amount,
                    receiptFileName, sizeof(receiptFileName));

    setColor(10);
    printf("\nDEPOSIT SUCCESSFUL!\n");
    printf("TRANSACTION ID  : %s\n", transactionId);
    printf("UPDATED BALANCE : Rs. %.2f\n", currentAccount.balance);
    printf("RECEIPT SAVED   : %s\n", receiptFileName);
    resetColor();
    playSuccessSound();

    footer();
    return pauseScreen();
}

int withdrawMoney(void) {
    char amountText[32];
    char transactionId[16];
    char timestamp[32];
    char receiptFileName[128];
    double amount;

    header();
    setColor(12);
    printf("WITHDRAW MONEY\n\n");
    resetColor();

    if (!readTimedLine("Enter Amount : Rs. ", amountText, sizeof(amountText),
                       INPUT_AMOUNT, 0, 18)) {
        return 0;
    }

    if (!parseMoney(amountText, &amount, 0, MAX_TRANSACTION_AMOUNT)) {
        printOperationFailure("INVALID AMOUNT!");
        footer();
        return pauseScreen();
    }

    if (amount > currentAccount.balance) {
        printOperationFailure("INSUFFICIENT BALANCE!");
        footer();
        return pauseScreen();
    }

    if (!loadingBar("VERIFYING FUNDS")) {
        return 0;
    }

    if (!loadingBar("DISPENSING CASH")) {
        return 0;
    }

    currentAccount.balance -= amount;
    if (!syncCurrentAccount() || !saveAccounts()) {
        currentAccount.balance += amount;
        syncCurrentAccount();
        printOperationFailure("ACCOUNT DATABASE UPDATE FAILED!");
        footer();
        return pauseScreen();
    }

    generateTransactionId(transactionId, sizeof(transactionId));
    getTimestamp(timestamp, sizeof(timestamp));
    saveTransactionForAccount(&currentAccount, transactionId, timestamp,
                              "WITHDRAW", amount, "ATM cash withdrawal");
    generateReceipt(transactionId, timestamp, "WITHDRAW", amount,
                    receiptFileName, sizeof(receiptFileName));

    setColor(10);
    printf("\nPLEASE COLLECT YOUR CASH\n");
    printf("TRANSACTION ID    : %s\n", transactionId);
    printf("REMAINING BALANCE : Rs. %.2f\n", currentAccount.balance);
    printf("RECEIPT SAVED     : %s\n", receiptFileName);
    resetColor();
    playSuccessSound();

    footer();
    return pauseScreen();
}

int transferFunds(void) {
    char receiverAccountNumber[ACCOUNT_NUMBER_SIZE];
    char amountText[32];
    char transactionId[16];
    char timestamp[32];
    char receiptFileName[128];
    char senderNote[96];
    char receiverNote[96];
    int receiverIndex;
    int confirmed = 0;
    double amount;
    Account receiverSnapshot;

    header();
    setColor(13);
    printf("TRANSFER FUNDS\n\n");
    resetColor();

    if (!readTimedLine("Receiver Account Number : ", receiverAccountNumber,
                       sizeof(receiverAccountNumber), INPUT_ACCOUNT_NUMBER,
                       0, ACCOUNT_NUMBER_SIZE - 1)) {
        return 0;
    }

    trimWhitespace(receiverAccountNumber);
    if (!isValidAccountNumber(receiverAccountNumber)) {
        printOperationFailure("INVALID ACCOUNT NUMBER!");
        footer();
        return pauseScreen();
    }

    if (strcmp(receiverAccountNumber, currentAccount.accountNumber) == 0) {
        printOperationFailure("TRANSFER TO SAME ACCOUNT IS NOT ALLOWED!");
        footer();
        return pauseScreen();
    }

    if (!loadingBar("VERIFYING RECEIVER")) {
        return 0;
    }

    receiverIndex = findAccountByNumber(receiverAccountNumber);
    if (receiverIndex < 0) {
        printOperationFailure("RECEIVER ACCOUNT NOT FOUND!");
        footer();
        return pauseScreen();
    }

    setColor(10);
    printf("\nReceiver Found : %s\n", users[receiverIndex].name);
    resetColor();

    if (!readTimedLine("Enter Amount : Rs. ", amountText, sizeof(amountText),
                       INPUT_AMOUNT, 0, 18)) {
        return 0;
    }

    if (!parseMoney(amountText, &amount, 0, MAX_TRANSACTION_AMOUNT)) {
        printOperationFailure("INVALID AMOUNT!");
        footer();
        return pauseScreen();
    }

    if (amount > currentAccount.balance) {
        printOperationFailure("INSUFFICIENT BALANCE!");
        footer();
        return pauseScreen();
    }

    printf("\nTransfer %.2f to %s (%s)\n",
           amount, users[receiverIndex].name, users[receiverIndex].accountNumber);

    if (!askYesNo("Confirm Transfer (Y/N) : ", &confirmed)) {
        return 0;
    }

    if (!confirmed) {
        setColor(14);
        printf("\nTRANSFER CANCELLED. NO BALANCE CHANGES MADE.\n");
        resetColor();
        footer();
        return pauseScreen();
    }

    if (!loadingBar("SECURING TRANSACTION")) {
        return 0;
    }

    currentAccount.balance -= amount;
    users[currentUserIndex].balance = currentAccount.balance;
    users[receiverIndex].balance += amount;
    receiverSnapshot = users[receiverIndex];

    if (!saveAccounts()) {
        users[currentUserIndex].balance += amount;
        currentAccount.balance += amount;
        users[receiverIndex].balance -= amount;
        printOperationFailure("ACCOUNT DATABASE UPDATE FAILED!");
        footer();
        return pauseScreen();
    }

    generateTransactionId(transactionId, sizeof(transactionId));
    getTimestamp(timestamp, sizeof(timestamp));

    snprintf(senderNote, sizeof(senderNote), "To %s (%s)",
             receiverSnapshot.name, receiverSnapshot.accountNumber);
    snprintf(receiverNote, sizeof(receiverNote), "From %s (%s)",
             currentAccount.name, currentAccount.accountNumber);

    saveTransactionForAccount(&currentAccount, transactionId, timestamp,
                              "TRANSFER OUT", amount, senderNote);
    saveTransactionForAccount(&receiverSnapshot, transactionId, timestamp,
                              "TRANSFER IN", amount, receiverNote);
    generateReceipt(transactionId, timestamp, "TRANSFER OUT", amount,
                    receiptFileName, sizeof(receiptFileName));

    setColor(10);
    printf("\nTRANSFER SUCCESSFUL!\n");
    printf("TRANSACTION ID    : %s\n", transactionId);
    printf("TRANSFERRED TO    : %s\n", receiverSnapshot.accountNumber);
    printf("REMAINING BALANCE : Rs. %.2f\n", currentAccount.balance);
    printf("RECEIPT SAVED     : %s\n", receiptFileName);
    resetColor();
    playSuccessSound();

    footer();
    return pauseScreen();
}

int lineContainsTransactionType(const char *line) {
    return strstr(line, "DEPOSIT") != NULL ||
           strstr(line, "WITHDRAW") != NULL ||
           strstr(line, "TRANSFER") != NULL;
}

void colorForTransactionType(const char *transactionType) {
    if (strstr(transactionType, "DEPOSIT") != NULL ||
        strstr(transactionType, "TRANSFER IN") != NULL) {
        setColor(10);
    } else if (strstr(transactionType, "WITHDRAW") != NULL ||
               strstr(transactionType, "TRANSFER OUT") != NULL) {
        setColor(12);
    } else if (strstr(transactionType, "TRANSFER") != NULL) {
        setColor(11);
    } else {
        resetColor();
    }
}

int displayStructuredHistoryLine(const char *line) {
    char copy[LINE_SIZE];
    char *fields[9];

    strncpy(copy, line, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';
    removeNewline(copy);

    if (strncmp(copy, "TXN", 3) != 0) {
        return 0;
    }

    if (!splitFields(copy, fields, 9)) {
        return 0;
    }

    if (strcmp(fields[2], currentAccount.accountNumber) != 0) {
        return 0;
    }

    colorForTransactionType(fields[5]);
    printf("%s | %s | %s | Rs. %s | Balance: Rs. %s",
           fields[0], fields[1], fields[5], fields[6], fields[7]);
    if (fields[8][0] != '\0') {
        printf(" | %s", fields[8]);
    }
    printf("\n");
    resetColor();

    return 1;
}

int transactionHistory(void) {
    FILE *file;
    char line[LINE_SIZE];
    char pendingLegacyLine[LINE_SIZE] = "";
    int displayed = 0;

    header();

    setColor(11);
    printf("TRANSACTION HISTORY\n\n");
    printRepeated("═", HEADER_WIDTH + 2);
    printf("\n");
    resetColor();

    file = fopen(TRANSACTION_FILE, "r");
    if (file == NULL) {
        setColor(12);
        printf("NO TRANSACTION RECORDS FOUND!\n");
        resetColor();
        footer();
        return pauseScreen();
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char cleanLine[LINE_SIZE];

        if (displayStructuredHistoryLine(line)) {
            displayed++;
            pendingLegacyLine[0] = '\0';
            continue;
        }

        strncpy(cleanLine, line, sizeof(cleanLine) - 1);
        cleanLine[sizeof(cleanLine) - 1] = '\0';
        removeNewline(cleanLine);

        if (lineContainsTransactionType(cleanLine)) {
            strncpy(pendingLegacyLine, cleanLine,
                    sizeof(pendingLegacyLine) - 1);
            pendingLegacyLine[sizeof(pendingLegacyLine) - 1] = '\0';

            if (strstr(cleanLine, currentAccount.name) != NULL ||
                strstr(cleanLine, currentAccount.accountNumber) != NULL) {
                colorForTransactionType(cleanLine);
                printf("%s\n", cleanLine);
                resetColor();
                displayed++;
                pendingLegacyLine[0] = '\0';
            }
            continue;
        }

        if (strstr(cleanLine, "USER :") != NULL) {
            if (strstr(cleanLine, currentAccount.name) != NULL) {
                colorForTransactionType(pendingLegacyLine);
                if (pendingLegacyLine[0] != '\0') {
                    printf("%s | %s\n", pendingLegacyLine, cleanLine);
                } else {
                    printf("%s\n", cleanLine);
                }
                resetColor();
                displayed++;
            }
            pendingLegacyLine[0] = '\0';
        }
    }

    fclose(file);

    if (displayed == 0) {
        setColor(14);
        printf("No transactions found for this account.\n");
        resetColor();
    }

    footer();
    return pauseScreen();
}

int changePin(void) {
    char oldPin[PIN_SIZE];
    char newPin[PIN_SIZE];

    header();
    setColor(13);
    printf("CHANGE SECURITY PIN\n\n");
    resetColor();

    if (!readTimedLine("Enter Current PIN : ", oldPin, sizeof(oldPin),
                       INPUT_DIGITS, 1, PIN_LENGTH)) {
        return 0;
    }

    if (!isDigitsExact(oldPin, PIN_LENGTH) ||
        strcmp(oldPin, currentAccount.pin) != 0) {
        printOperationFailure("INCORRECT CURRENT PIN!");
        footer();
        return pauseScreen();
    }

    if (!readTimedLine("Enter New PIN     : ", newPin, sizeof(newPin),
                       INPUT_DIGITS, 1, PIN_LENGTH)) {
        return 0;
    }

    if (!isDigitsExact(newPin, PIN_LENGTH)) {
        printOperationFailure("NEW PIN MUST CONTAIN EXACTLY 4 DIGITS!");
        footer();
        return pauseScreen();
    }

    strncpy(currentAccount.pin, newPin, PIN_SIZE - 1);
    currentAccount.pin[PIN_SIZE - 1] = '\0';

    if (!syncCurrentAccount() || !saveAccounts()) {
        printOperationFailure("PIN UPDATE FAILED!");
        footer();
        return pauseScreen();
    }

    setColor(10);
    printf("\nPIN CHANGED SUCCESSFULLY!\n");
    resetColor();
    playSuccessSound();

    footer();
    return pauseScreen();
}

void drawMenu(void) {
    setColor(11);
    printf("╔════════════════════════════════════════╗\n");
    setColor(14);
    printf("║ [1] CHECK BALANCE                     ║\n");
    setColor(10);
    printf("║ [2] DEPOSIT MONEY                     ║\n");
    setColor(12);
    printf("║ [3] WITHDRAW MONEY                    ║\n");
    setColor(13);
    printf("║ [4] TRANSFER FUNDS                    ║\n");
    setColor(11);
    printf("║ [5] TRANSACTION HISTORY               ║\n");
    setColor(9);
    printf("║ [6] CHANGE SECURITY PIN               ║\n");
    setColor(10);
    printf("║ [7] EXIT TERMINAL                     ║\n");
    setColor(11);
    printf("╚════════════════════════════════════════╝\n");
    resetColor();
}

int menu(void) {
    while (sessionActive && !sessionExpired) {
        char choiceText[8];
        int choice;

        header();
        drawMenu();

        if (!readTimedLine("\nEnter Choice : ", choiceText, sizeof(choiceText),
                           INPUT_DIGITS, 0, 1)) {
            return 0;
        }

        if (strlen(choiceText) != 1 || choiceText[0] < '1' ||
            choiceText[0] > '7') {
            printOperationFailure("INVALID CHOICE!");
            footer();
            if (!pauseScreen()) {
                return 0;
            }
            continue;
        }

        choice = choiceText[0] - '0';

        switch (choice) {
            case 1:
                if (!checkBalance()) {
                    return 0;
                }
                break;

            case 2:
                if (!depositMoney()) {
                    return 0;
                }
                break;

            case 3:
                if (!withdrawMoney()) {
                    return 0;
                }
                break;

            case 4:
                if (!transferFunds()) {
                    return 0;
                }
                break;

            case 5:
                if (!transactionHistory()) {
                    return 0;
                }
                break;

            case 6:
                if (!changePin()) {
                    return 0;
                }
                break;

            case 7:
                header();
                loadingBar("TERMINATING SECURE SESSION");
                sessionActive = 0;
                currentUserIndex = -1;
                memset(&currentAccount, 0, sizeof(currentAccount));
                clearScreen();
                setColor(10);
                printf("\nTHANK YOU FOR USING ATM SIMULATOR\n");
                printf("HAVE A SAFE DAY!\n\n");
                resetColor();
                flushOutput();
                exit(0);

            default:
                break;
        }
    }

    return 0;
}

int main(void) {
    configureConsole();
    loadAccounts();

    while (1) {
        sessionActive = 0;
        sessionExpired = 0;
        currentUserIndex = -1;
        memset(&currentAccount, 0, sizeof(currentAccount));

        insertCardAnimation();
        bootScreen();

        if (loginSystem()) {
            menu();
        }
    }

    return 0;
}
