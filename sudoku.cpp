#include <iostream>
#include <string>
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
        for (int c = 0; c < SIZE; c++) {
            cout << board[r][c] << " ";
        }
        cout << "\n";
    }
}

int main() {
    int board[SIZE][SIZE];
    string line;

    cout << "Enter Sudoku puzzle, 9 lines with 9 chars each (digits 1-9, 0 or . for empty):\n";

    for (int i = 0; i < SIZE; i++) {
        getline(cin, line);
        if (line.size() < SIZE) {
            cerr << "Invalid input line length\n";
            return 1;
        }
        for (int j = 0; j < SIZE; j++) {
            char ch = line[j];
            if (ch >= '1' && ch <= '9')
                board[i][j] = ch - '0';
            else
                board[i][j] = 0; // treat '.' or '0' or others as empty
        }
    }

    if (solveSudoku(board)) {
        cout << "\nSolved Sudoku:\n";
        printBoard(board);
    } else {
        cout << "No solution exists.\n";
    }

    return 0;
}
