#include <iostream>
#include <fstream>
#include <string>
#include <vector>
using namespace std;

const int SIZE = 9;

bool isValid(int board[SIZE][SIZE], int row, int col, int num) {
    for (int c = 0; c < SIZE; c++)
        if (board[row][c] == num) return false;
    for (int r = 0; r < SIZE; r++)
        if (board[r][col] == num) return false;
    int boxStartRow = (row / 3) * 3;
    int boxStartCol = (col / 3) * 3;
    for (int r = boxStartRow; r < boxStartRow + 3; r++)
        for (int c = boxStartCol; c < boxStartCol + 3; c++)
            if (board[r][c] == num) return false;
    return true;
}

bool solveSudoku(int board[SIZE][SIZE]) {
    int row = -1, col = -1;
    bool emptyFound = false;
    for (int r = 0; r < SIZE && !emptyFound; r++) {
        for (int c = 0; c < SIZE && !emptyFound; c++) {
            if (board[r][c] == 0) {
                row = r; col = c;
                emptyFound = true;
            }
        }
    }
    if (!emptyFound) return true;
    for (int num = 1; num <= 9; num++) {
        if (isValid(board, row, col, num)) {
            board[row][col] = num;
            if (solveSudoku(board)) return true;
            board[row][col] = 0;
        }
    }
    return false;
}

void printBoard(int board[SIZE][SIZE]) {
    for (int r = 0; r < SIZE; r++) {
        if (r % 3 == 0) cout << "+-------+-------+-------+\n";
        for (int c = 0; c < SIZE; c++) {
            if (c % 3 == 0) cout << "| ";
            cout << board[r][c] << " ";
        }
        cout << "|\n";
    }
    cout << "+-------+-------+-------+\n";
}

bool readPuzzle(istream& in, int board[SIZE][SIZE]) {
    string line;
    vector<string> lines;
    while ((int)lines.size() < SIZE && getline(in, line)) {
        if ((int)line.size() < SIZE) continue; // skip short lines
        lines.push_back(line);
    }
    if ((int)lines.size() != SIZE) return false; // incomplete puzzle

    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            char ch = lines[i][j];
            if (ch >= '1' && ch <= '9')
                board[i][j] = ch - '0';
            else
                board[i][j] = 0;
        }
    }
    return true;
}

int main(int argc, char* argv[]) {
    istream* in = &cin;
    ifstream file;

    if (argc > 1) {
        file.open(argv[1]);
        if (!file) {
            cerr << "Error opening file: " << argv[1] << "\n";
            return 1;
        }
        in = &file;
    } else {
        cout << "Enter Sudoku puzzles one by one (9 lines each). Ctrl+D to stop.\n";
    }

    int puzzleCount = 0;
    int board[SIZE][SIZE];

    while (readPuzzle(*in, board)) {
        puzzleCount++;
        cout << "\nPuzzle #" << puzzleCount << ":\n";
        cout << "Input:\n";
        printBoard(board);

        if (solveSudoku(board)) {
            cout << "Solved:\n";
            printBoard(board);
        } else {
            cout << "No solution exists for this puzzle.\n";
        }
        cout << endl;
    }

    if (puzzleCount == 0) {
        cout << "No puzzles found in input.\n";
    }

    return 0;
}
