// cRZSparseMatrix<T> — the sparse two-key table behind the window grids (instantiated for the grid's cell data,
// its column/row format extras and the other grid's cell/column/row records: ~12 functions each in the binary).
// Layout of the original (0x30 bytes): +0 an ordered map row -> MatrixRow, +0x18 the number of cells, +0x1c..+0x28 the
// accepted index limits (min row, max row, min column, max column; default +-2^31), +0x2c a flag that Shutdown tests.
// A MatrixRow is an ordered map column -> value. Every walk is in key order (rows ascending, then columns ascending).
//
// Quirks kept: InsertCell refuses indices outside the limits; Shutdown only clears when the flag is set (nothing in the
// recovered code sets it, so it does nothing, and the destructor does the real clearing); RemoveCol visits every row and
// drops rows that become empty; RemoveRow subtracts the row's size from the cell count; the "used" queries answer
// INT_MIN when nothing is stored.
#pragma once
#include <climits>
#include <cstdint>
#include <iterator>
#include <map>

template <class T>
class cRZSparseMatrix {
public:
    using Row = std::map<int, T>;                    // column -> value
    using Rows = std::map<int, Row>;                 // row -> columns

    struct UserCell {                                // the record GetCellRange fills (16 bytes for pointer values)
        int col;
        int row;
        T value;
    };

    cRZSparseMatrix() = default;
    cRZSparseMatrix(const cRZSparseMatrix&) = delete;
    cRZSparseMatrix& operator=(const cRZSparseMatrix&) = delete;
    ~cRZSparseMatrix() { Shutdown(); rows_.clear(); }

    bool Shutdown() {
        if (flag_) {
            rows_.clear();
            count_ = 0;
            flag_ = false;
        }
        return true;
    }

    int Count() const { return count_; }
    void SetLimits(int minRow, int maxRow, int minCol, int maxCol) { minRow_ = minRow; maxRow_ = maxRow; minCol_ = minCol; maxCol_ = maxCol; }
    // Drops everything (what the grid does by hand when it disposes of its cells).
    void Clear() { rows_.clear(); count_ = 0; }

    bool GetCellPtr(int col, int row, T** out) {
        auto r = rows_.find(row);
        if (r == rows_.end()) return false;
        auto c = r->second.find(col);
        if (c == r->second.end()) return false;
        if (out) *out = &c->second;
        return true;
    }

    // Stores `value`; with an existing cell the old value goes to `previous` (if given) and is replaced.
    bool InsertCell(int col, int row, const T& value, T* previous) {
        if (row < minRow_ || col < minCol_ || maxRow_ < row || maxCol_ < col) return false;
        Row& cells = rows_[row];
        auto existing = cells.find(col);
        if (existing != cells.end()) {
            if (previous) *previous = existing->second;
            existing->second = value;
            return true;
        }
        cells.emplace(col, value);
        ++count_;
        return true;
    }

    bool RemoveCell(int col, int row, T* removed) {
        auto r = rows_.find(row);
        if (r == rows_.end()) return false;
        auto c = r->second.find(col);
        if (c == r->second.end()) return false;
        if (removed) *removed = c->second;
        r->second.erase(c);
        --count_;
        if (r->second.empty()) rows_.erase(r);
        return true;
    }

    bool RemoveCol(int firstCol, int count) {
        for (auto r = rows_.begin(); r != rows_.end();) {
            for (int col = firstCol; col < firstCol + count; ++col) {
                auto c = r->second.find(col);
                if (c != r->second.end()) {
                    r->second.erase(c);
                    --count_;
                }
            }
            if (r->second.empty()) r = rows_.erase(r);
            else ++r;
        }
        return true;
    }

    bool RemoveRow(int firstRow, int count) {
        for (int row = firstRow; row < firstRow + count; ++row) {
            auto r = rows_.find(row);
            if (r == rows_.end()) continue;
            count_ -= static_cast<int>(r->second.size());
            rows_.erase(r);
        }
        return true;
    }

    // Cells with minCol <= col <= maxCol and minRow <= row <= maxRow, rows first.
    int GetCellCountForRange(int minCol, int maxCol, int minRow, int maxRow) const {
        int total = 0;
        for (const auto& [row, cells] : rows_) {
            if (row < minRow) continue;
            if (row > maxRow) break;
            for (const auto& [col, value] : cells) {
                if (col < minCol) continue;
                if (col > maxCol) break;
                ++total;
            }
        }
        return total;
    }
    int GetCellRange(int minCol, int maxCol, int minRow, int maxRow, UserCell* out) const {
        int total = 0;
        for (const auto& [row, cells] : rows_) {
            if (row < minRow) continue;
            if (row > maxRow) break;
            for (const auto& [col, value] : cells) {
                if (col < minCol) continue;
                if (col > maxCol) break;
                if (out) out[total] = UserCell{col, row, value};
                ++total;
            }
        }
        return total;
    }

    // The smallest (largest) column that holds a cell, and the first row that holds it; INT_MIN when empty.
    int GetMinUsedCol(int& rowOfMin) const {
        rowOfMin = INT_MIN;
        int best = INT_MIN;
        for (const auto& [row, cells] : rows_) {
            const int first = cells.begin()->first;
            if (best == INT_MIN || first < best) {
                rowOfMin = row;
                best = first;
            }
        }
        return best;
    }
    int GetMaxUsedCol(int& rowOfMax) const {
        rowOfMax = INT_MIN;
        int best = INT_MIN;
        for (const auto& [row, cells] : rows_) {
            const int last = cells.rbegin()->first;
            if (best == INT_MIN || best < last) {
                rowOfMax = row;
                best = last;
            }
        }
        return best;
    }
    // Rows that have a cell in `col`: the first and the last of them. False (with 0x7fffffff / INT_MIN) when none.
    bool GetMinMaxUsedRowForCol(int col, int& minRow, int& maxRow) const {
        minRow = INT_MAX;
        maxRow = INT_MIN;
        bool found = false;
        for (const auto& [row, cells] : rows_) {
            if (cells.find(col) == cells.end()) continue;
            if (!found) minRow = row;
            maxRow = row;
            found = true;
        }
        return found;
    }
    bool GetMinMaxUsedColForRow(int row, int& minCol, int& maxCol) const {
        minCol = INT_MAX;
        maxCol = INT_MIN;
        auto r = rows_.find(row);
        if (r == rows_.end()) return false;
        minCol = r->second.begin()->first;
        maxCol = r->second.rbegin()->first;
        return true;
    }
    int GetRowCountForCol(int col) const {
        int total = 0;
        for (const auto& [row, cells] : rows_) total += cells.find(col) != cells.end() ? 1 : 0;
        return total;
    }

    // Walks every cell in order (rows ascending, columns ascending).
    class iterator {
    public:
        iterator(Rows* rows, typename Rows::iterator row, typename Row::iterator cell) : rows_(rows), row_(row), cell_(cell) { Settle(); }
        T& operator*() const { return cell_->second; }
        T* operator->() const { return &cell_->second; }
        int col() const { return cell_->first; }
        int row() const { return row_->first; }
        iterator& operator++() { ++cell_; Settle(); return *this; }
        bool operator==(const iterator& o) const { return row_ == o.row_ && (row_ == end_() || cell_ == o.cell_); }
        bool operator!=(const iterator& o) const { return !(*this == o); }

    private:
        typename Rows::iterator end_() const { return rows_->end(); }
        void Settle() {
            while (row_ != end_() && cell_ == row_->second.end()) {
                ++row_;
                if (row_ != end_()) cell_ = row_->second.begin();
            }
        }
        Rows* rows_;
        typename Rows::iterator row_;
        typename Row::iterator cell_;
    };
    iterator begin() { return iterator(&rows_, rows_.begin(), rows_.empty() ? typename Row::iterator() : rows_.begin()->second.begin()); }
    iterator end() { return iterator(&rows_, rows_.end(), typename Row::iterator()); }

    // Walks the cells of one column, one per row that has it.
    class col_iterator {
    public:
        col_iterator(Rows* rows, int col, typename Rows::iterator row) : rows_(rows), col_(col), row_(row) { Settle(); }
        T& operator*() const { return row_->second.find(col_)->second; }
        int row() const { return row_->first; }
        col_iterator& operator++() { ++row_; Settle(); return *this; }
        bool operator!=(const col_iterator& o) const { return row_ != o.row_; }
        bool operator==(const col_iterator& o) const { return row_ == o.row_; }

    private:
        void Settle() { while (row_ != rows_->end() && row_->second.find(col_) == row_->second.end()) ++row_; }
        Rows* rows_;
        int col_;
        typename Rows::iterator row_;
    };
    col_iterator col_begin(int col) { return col_iterator(&rows_, col, rows_.begin()); }
    col_iterator col_end(int col) { return col_iterator(&rows_, col, rows_.end()); }

private:
    Rows rows_;                      // +0
    int count_ = 0;                  // +0x18
    int minRow_ = INT_MIN + 1, maxRow_ = INT_MAX, minCol_ = INT_MIN + 1, maxCol_ = INT_MAX;   // +0x1c..+0x28
    bool flag_ = false;              // +0x2c
};
