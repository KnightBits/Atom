#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <curses.h>
#include <csignal>
#include <unistd.h>
#include <stack>

std::vector<std::string> buffer;
std::stack<std::pair<int, std::string>> undoStack; // Стек для отмены
std::stack<std::pair<int, std::string>> redoStack; // Стек для повтора
std::vector<std::string> clipboard; // Буфер для копирования строк
int currentLine = 0;
int currentColumn = 0;
std::string filename;
bool running = true;

enum Mode { NORMAL, INSERT, COMMAND };
Mode currentMode = NORMAL;

void LoadFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open file: " << filename << std::endl;
        return;
    }

    buffer.clear();
    std::string line;
    while (std::getline(file, line)) {
        buffer.push_back(line);
    }

    file.close();
}

void SaveFile(const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Cannot open file: " << filename << std::endl;
        return;
    }

    for (const auto& line : buffer) {
        file << line << std::endl;
    }

    file.close();
}

void DisplayStatus() {
    move(LINES - 1, 0);
    clrtoeol();
    printw("-- %s -- %s %d,%d",
           (currentMode == NORMAL) ? "NORMAL" : (currentMode == INSERT) ? "INSERT" : "COMMAND",
           filename.c_str(), currentLine + 1, currentColumn + 1);
}

void DisplayBuffer() {
    clear(); // Очистка окна

    int y = 0;
    for (const auto& line : buffer) {
        int x = 0;
        for (const char& ch : line) {
            if (ch == 'i' || ch == 'o' || ch == 'f' || ch == 'r') { // Пример ключевых слов
                attron(COLOR_PAIR(1));
                mvaddch(y, x, ch);
                attroff(COLOR_PAIR(1));
            } else {
                mvaddch(y, x, ch);
            }
            ++x;
        }
        ++y;
    }

    move(currentLine, currentColumn);
    refresh();
}

void MoveCursor(int dx, int dy) {
    int newColumn = currentColumn + dx;
    int newLine = currentLine + dy;

    // Ограничения на перемещение курсора
    if (newLine < 0) {
        newLine = 0;
    } else if (newLine >= buffer.size()) {
        newLine = buffer.size() - 1;
    }

    if (newColumn < 0) {
        newColumn = 0;
    } else if (newColumn >= buffer[newLine].size()) {
        newColumn = buffer[newLine].size();
    }

    currentLine = newLine;
    currentColumn = newColumn;
}

void Undo() {
    if (!undoStack.empty()) {
        auto action = undoStack.top();
        undoStack.pop();

        int line = action.first;
        std::string& text = action.second;

        redoStack.push({line, buffer[line]});
        buffer[line] = text;
        DisplayBuffer();
    }
}

void Redo() {
    if (!redoStack.empty()) {
        auto action = redoStack.top();
        redoStack.pop();

        int line = action.first;
        std::string& text = action.second;

        undoStack.push({line, buffer[line]});
        buffer[line] = text;
        DisplayBuffer();
    }
}

void CopyLine() {
    clipboard.clear();
    clipboard.push_back(buffer[currentLine]);
}

void CutLine() {
    if (currentLine < buffer.size()) {
        undoStack.push({currentLine, buffer[currentLine]});
        clipboard.clear();
        clipboard.push_back(buffer[currentLine]);
        buffer.erase(buffer.begin() + currentLine);
        if (currentLine >= buffer.size()) {
            currentLine = buffer.size() - 1;
        }
        DisplayBuffer();
    }
}

void PasteLine() {
    if (!clipboard.empty()) {
        undoStack.push({currentLine, buffer[currentLine]});
        buffer.insert(buffer.begin() + currentLine, clipboard.begin(), clipboard.end());
        DisplayBuffer();
    }
}

void Replace() {
    echo();
    curs_set(1);

    mvprintw(LINES - 1, 0, ":s/");
    char findQuery[256];
    getnstr(findQuery, 255);

    mvprintw(LINES - 1, 0, ":s/%s/", findQuery);
    char replaceQuery[256];
    getnstr(replaceQuery, 255);

    noecho();
    curs_set(0);

    for (auto& line : buffer) {
        size_t pos = 0;
        while ((pos = line.find(findQuery, pos)) != std::string::npos) {
            line.replace(pos, strlen(findQuery), replaceQuery);
            pos += strlen(replaceQuery);
        }
    }

    DisplayBuffer();
}

void Search() {
    echo();
    curs_set(1);

    mvprintw(LINES - 1, 0, "/");
    char query[256];
    getnstr(query, 255);
    std::string searchQuery = query;
    int searchPos = -1;

    noecho();
    curs_set(0);

    for (int i = currentLine; i < buffer.size(); ++i) {
        size_t pos = buffer[i].find(searchQuery, (i == currentLine && searchPos != -1) ? searchPos + 1 : 0);
        if (pos != std::string::npos) {
            currentLine = i;
            currentColumn = pos;
            DisplayBuffer();
            return;
        }
    }

    move(LINES - 1, 0);
    clrtoeol();
    printw("Pattern not found: %s", searchQuery.c_str());
    refresh();
}

void ProcessCommand(const std::string& command) {
    if (command == "w") {
        SaveFile(filename);
        move(LINES - 1, 0);
        clrtoeol();
        printw("File saved");
    } else if (command == "q") {
        endwin();
        exit(0);
    } else if (command == "u") {
        Undo();
    } else if (command == "r") {
        Redo();
    } else if (command == "/") {
        Search();
    } else if (command == "n") {
        NextMatch();
    } else if (command == "N") {
        PreviousMatch();
    } else if (command == ":s") {
        Replace();
    } else if (command.substr(0, 2) == "e ") {
        filename = command.substr(2);
        LoadFile(filename);
        DisplayBuffer();
    }
}

void ProcessInput() {
    std::string commandBuffer;
    int ch;

    while (running) {
        ch = getch();

        if (currentMode == NORMAL) {
            switch (ch) {
                case 'i':
                    currentMode = INSERT;
                    move(LINES - 1, 0);
                    clrtoeol();
                    printw("-- INSERT --");
                    move(currentLine, currentColumn);
                    break;
                case ':':
                    currentMode = COMMAND;
                    move(LINES - 1, 0);
                    clrtoeol();
                    printw(":");
                    break;
                case 'h':
                    MoveCursor(-1, 0);
                    break;
                case 'j':
                    MoveCursor(0, 1);
                    break;
                case 'k':
                    MoveCursor(0, -1);
                    break;
                case 'l':
                    MoveCursor(1, 0);
                    break;
                case 'd':
                    if (getch() == 'd') { // dd для удаления строки
                        CutLine();
                    }
                    break;
                case 'y':
                    if (getch() == 'y') { // yy для копирования строки
                        CopyLine();
                    }
                    break;
                case 'p':
                    PasteLine();
                    break;
                case 'u':
                    Undo();
                    break;
                case '/':
                    Search();
                    break;
                case 'n':
                    NextMatch();
                    break;
                case 'N':
                    PreviousMatch();
                    break;
                case 26: // Ctrl+Z
                    running = false;
                    break;
                case 18: // Ctrl+R
                    Redo();
                    break;
            }
        } else if (currentMode == INSERT) {
            if (ch == 27) { // ESC key
                currentMode = NORMAL;
                move(LINES - 1, 0);
                clrtoeol();
                printw("-- NORMAL --");
            } else {
                if (ch == KEY_BACKSPACE || ch == 127) {
                    if (!buffer[currentLine].empty() && currentColumn > 0) {
                        undoStack.push({currentLine, buffer[currentLine]});
                        buffer[currentLine].erase(--currentColumn, 1);
                        move(currentLine, currentColumn);
                        delch();
                    }
                } else {
                    undoStack.push({currentLine, buffer[currentLine]});
                    buffer[currentLine].insert(currentColumn++, 1, ch);
                    move(currentLine, currentColumn);
                    insch(ch);
                }
            }
        } else if (currentMode == COMMAND) {
            if (ch == '\r') { // Enter key
                ProcessCommand(commandBuffer);
                commandBuffer.clear();
                currentMode = NORMAL;
                move(LINES - 1, 0);
                clrtoeol();
                printw("-- NORMAL --");
            } else {
                commandBuffer += ch;
                addch(ch);
            }
        }

        move(currentLine, currentColumn);
        refresh();
    }
}

void SignalHandler(int signum) {
    if (signum == SIGTSTP) {
        endwin();
        running = false;
        raise(SIGSTOP);
    } else if (signum == SIGINT) {
        endwin();
        exit(0);
    }
}

int main() {
    signal(SIGTSTP, SignalHandler);
    signal(SIGINT, SignalHandler);

    // Запрос имени файла у пользователя
    std::cout << "Enter filename: ";
    std::getline(std::cin, filename);
    LoadFile(filename);

    // Инициализация ncurses
    initscr();
    start_color();
    init_pair(1, COLOR_RED, COLOR_BLACK); // Настройка цветовой пары

    DisplayBuffer();
    ProcessInput();

    // Завершение работы ncurses
    endwin();

    return 0;
}
