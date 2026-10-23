// cRZSparseMatrix<T> — sparse rows x columns storage used by the grid and tree-view controls
// (15 template methods per instantiation; cIGZWinGrid::CellData* at 0x1002545fa..0x10025d776,
// cIGZWinGrid::CellFormatExtra, WinGridCellData, WinGridColData, WinGridRowData). A cell is stored
// only when it is set, so a 1000x1000 grid costs what its used cells cost.
//
// Original layout (object start): +0x00 std::map<int, MatrixRow<T>> (root, end node, size),
// +0x18 int cellCount, +0x1c int minRow, +0x20 int maxRow, +0x24 int minCol, +0x28 int maxCol,
// +0x2c bool initialised. MatrixRow<T> is { int row; std::map<int, MatrixCell<T>> cells; } and
// MatrixCell<T> is { int col; T data; } — the column index is stored twice (map key + MatrixCell),
// exactly as the original does. InsertCell refuses rows/columns outside the limits; the limits
// default to [0x80000001, 0x7fffffff] (i.e. -2147483647..2147483647, not INT_MIN/INT_MAX, because
// -2147483648 is the "no column yet" sentinel of GetMinUsedCol / GetMaxUsedCol).
//
// Quirks kept: GetMinUsedCol starts at -2147483648 and stays there on an empty matrix (0x100256296);
// GetMaxUsedCol does the same (0x100256308); GetMinMaxUsedRowForCol answers 0 and leaves
// INT_MAX/INT_MIN in the outputs when the column is unused (0x100257dde); RemoveCol erases a row as
// soon as its last cell goes (0x1002573d0) while RemoveRow subtracts the row's size from the total
// before erasing it (0x10025796e); Shutdown only clears when the matrix was initialised (0x10025d776).
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>

namespace rz {

// The cell record handed out by GetCellRange: column, row, value (16 bytes, the original's UserCell).
template <class T>
struct MatrixUserCell {
    std::int32_t col = 0;
    std::int32_t row = 0;
    T value{};
};

template <class T>
class cRZSparseMatrix {
public:
    using RowMap = std::map<std::int32_t, T>;

    static constexpr std::int32_t kNoLimit = -2147483647;   // 0x80000001, see the header comment
    static constexpr std::int32_t kLimitMax = 2147483647;   // 0x7fffffff
    static constexpr std::int32_t kNoColumn = -2147483648;  // 0x80000000 sentinel of GetMin/MaxUsedCol

    cRZSparseMatrix() = default;

    // Limits: the original writes the four ints directly from its owner (cGZWinGrid::MakeRoom and
    // friends inline the store), so no symbol exists for it. Same order as the fields.
    void SetLimits(std::int32_t minRow, std::int32_t maxRow, std::int32_t minCol, std::int32_t maxCol)
    {
        minRow_ = minRow;
        maxRow_ = maxRow;
        minCol_ = minCol;
        maxCol_ = maxCol;
        initialised_ = true;
    }
    void GetLimits(std::int32_t& minRow, std::int32_t& maxRow, std::int32_t& minCol, std::int32_t& maxCol) const
    {
        minRow = minRow_;
        maxRow = maxRow_;
        minCol = minCol_;
        maxCol = maxCol_;
    }
    bool IsInitialised() const { return initialised_; }
    std::int32_t GetCellCount() const { return count_; }
    std::int32_t GetRowCount() const { return static_cast<std::int32_t>(rows_.size()); }
    bool IsEmpty() const { return rows_.empty(); }

    // 0x10025d776: clears only an initialised matrix and drops the initialised flag.
    bool Shutdown()
    {
        if (!initialised_) return true;
        rows_.clear();
        count_ = 0;
        initialised_ = false;
        return true;
    }

    // 0x1002545fa: true when the cell exists; *out (optional) receives a pointer to its value.
    bool GetCellPtr(std::int32_t col, std::int32_t row, T** out = nullptr)
    {
        auto r = rows_.find(row);
        if (r == rows_.end()) return false;
        auto c = r->second.find(col);
        if (c == r->second.end()) return false;
        if (out) *out = &c->second;
        return true;
    }
    bool GetCellPtr(std::int32_t col, std::int32_t row, const T** out) const
    {
        auto r = rows_.find(row);
        if (r == rows_.end()) return false;
        auto c = r->second.find(col);
        if (c == r->second.end()) return false;
        if (out) *out = &c->second;
        return true;
    }

    // 0x100258d88: false when the position is outside the limits. An existing cell is overwritten
    // (previous value to *previous, count unchanged); a new cell increments the count.
    bool InsertCell(std::int32_t col, std::int32_t row, const T& value, T* previous = nullptr)
    {
        if (row < minRow_ || row > maxRow_ || col < minCol_ || col > maxCol_) return false;
        auto& cells = rows_[row];
        auto c = cells.find(col);
        if (c != cells.end()) {
            if (previous) *previous = c->second;
            c->second = value;
            return true;
        }
        cells.emplace(col, value);
        ++count_;
        return true;
    }

    // 0x100256cf2: true when the cell was there; *out (optional) receives the removed value. The row
    // is erased as soon as it has no cells left.
    bool RemoveCell(std::int32_t col, std::int32_t row, T* out = nullptr)
    {
        auto r = rows_.find(row);
        if (r == rows_.end()) return false;
        auto c = r->second.find(col);
        if (c == r->second.end()) return false;
        if (out) *out = c->second;
        r->second.erase(c);
        --count_;
        if (r->second.empty()) rows_.erase(r);
        return true;
    }

    // 0x1002573d0: erases `count` columns starting at `col` in every row, dropping emptied rows.
    // The cell count is decremented per erased cell (there is no separate row bookkeeping).
    bool RemoveCol(std::int32_t col, std::int32_t count)
    {
        if (count <= 0) return true;
        for (auto it = rows_.begin(); it != rows_.end();) {
            auto& cells = it->second;
            for (std::int32_t c = col; c < col + count; ++c) {
                auto cell = cells.find(c);
                if (cell == cells.end()) continue;
                cells.erase(cell);
                --count_;
            }
            it = cells.empty() ? rows_.erase(it) : std::next(it);
        }
        return true;
    }

    // 0x10025796e: erases `count` rows starting at `row`, subtracting each one's size from the total.
    bool RemoveRow(std::int32_t row, std::int32_t count)
    {
        if (count <= 0) return true;
        for (std::int32_t r = row; r < row + count; ++r) {
            auto it = rows_.find(r);
            if (it == rows_.end()) continue;
            count_ -= static_cast<std::int32_t>(it->second.size());
            rows_.erase(it);
        }
        return true;
    }

    // 0x100257214: number of cells inside [col0,col1] x [row0,row1]. Rows above row1 are skipped and
    // the walk stops at the first row past row1 (the original returns there).
    std::int32_t GetCellCountForRange(std::int32_t col0, std::int32_t col1, std::int32_t row0, std::int32_t row1) const
    {
        std::int32_t n = 0;
        for (auto r = rows_.lower_bound(row0); r != rows_.end() && r->first <= row1; ++r) {
            for (auto c = r->second.lower_bound(col0); c != r->second.end() && c->first <= col1; ++c) ++n;
        }
        return n;
    }

    // 0x1002572e2: the same walk, writing {col,row,value} records into `out` (which the caller sized
    // with GetCellCountForRange). `out` may be null to only walk.
    void GetCellRange(std::int32_t col0, std::int32_t col1, std::int32_t row0, std::int32_t row1,
                      MatrixUserCell<T>* out) const
    {
        std::int32_t n = 0;
        for (auto r = rows_.lower_bound(row0); r != rows_.end() && r->first <= row1; ++r) {
            for (auto c = r->second.lower_bound(col0); c != r->second.end() && c->first <= col1; ++c) {
                if (out) out[n] = MatrixUserCell<T>{c->first, r->first, c->second};
                ++n;
            }
        }
    }

    // 0x100256296: the leftmost used column (the first cell of each row), kNoColumn when empty.
    void GetMinUsedCol(std::int32_t& out) const
    {
        out = kNoColumn;
        std::int32_t best = kNoColumn;
        for (const auto& row : rows_) {
            if (row.second.empty()) continue;
            const std::int32_t col = row.second.begin()->first;
            if (best == kNoColumn || col < best) {
                out = col;
                best = col;
            }
        }
    }

    // 0x100256308: the rightmost used column (the last cell of each row), kNoColumn when empty.
    void GetMaxUsedCol(std::int32_t& out) const
    {
        out = kNoColumn;
        std::int32_t best = kNoColumn;
        for (const auto& row : rows_) {
            if (row.second.empty()) continue;
            const std::int32_t col = row.second.rbegin()->first;
            if (best == kNoColumn || best < col) {
                out = col;
                best = col;
            }
        }
    }

    // 0x100257dde: first and last row that have a cell in `col`. Answers 0 (and INT_MAX/INT_MIN) when
    // the column is unused.
    bool GetMinMaxUsedRowForCol(std::int32_t col, std::int32_t& minRow, std::int32_t& maxRow) const
    {
        minRow = kLimitMax;
        maxRow = kNoColumn;
        bool found = false;
        for (const auto& row : rows_) {
            if (row.second.find(col) == row.second.end()) continue;
            if (!found) {
                minRow = row.first;
                found = true;
            }
            maxRow = row.first;
        }
        return found;
    }

    // 0x1002583da: first and last used column of `row`; answers 0 and leaves INT_MAX/INT_MIN when the
    // row is absent.
    bool GetMinMaxUsedColForRow(std::int32_t row, std::int32_t& minCol, std::int32_t& maxCol) const
    {
        minCol = kLimitMax;
        maxCol = kNoColumn;
        auto it = rows_.find(row);
        if (it == rows_.end() || it->second.empty()) return false;
        minCol = it->second.begin()->first;
        maxCol = it->second.rbegin()->first;
        return true;
    }

    // 0x10025cb44: how many rows hold a cell in `col`.
    std::int32_t GetRowCountForCol(std::int32_t col) const
    {
        std::int32_t n = 0;
        for (const auto& row : rows_)
            if (row.second.find(col) != row.second.end()) ++n;
        return n;
    }

    // 0x10025c708 / 0x10025c778: walk every cell in row-major, column-minor order.
    class Iterator {
    public:
        Iterator() = default;
        Iterator(const cRZSparseMatrix* matrix, bool atEnd) : matrix_(matrix)
        {
            if (!matrix_) return;
            row_ = atEnd ? matrix_->rows_.end() : matrix_->rows_.begin();
            if (!atEnd) SkipEmptyRows();
        }
        MatrixUserCell<T> operator*() const
        {
            return MatrixUserCell<T>{cell_->first, row_->first, cell_->second};
        }
        Iterator& operator++()
        {
            if (!matrix_) return *this;
            ++cell_;
            if (cell_ == row_->second.end()) {
                ++row_;
                SkipEmptyRows();
            }
            return *this;
        }
        bool operator==(const Iterator& other) const { return AtEnd() == other.AtEnd(); }
        bool operator!=(const Iterator& other) const { return !(*this == other); }

    private:
        bool AtEnd() const { return !matrix_ || row_ == matrix_->rows_.end(); }
        void SkipEmptyRows()
        {
            while (row_ != matrix_->rows_.end() && row_->second.empty()) ++row_;
            if (row_ != matrix_->rows_.end()) cell_ = row_->second.begin();
        }
        const cRZSparseMatrix* matrix_ = nullptr;
        typename std::map<std::int32_t, RowMap>::const_iterator row_;
        typename RowMap::const_iterator cell_;
    };

    // 0x10025ac3e / 0x10025ad00: walk one column over the rows that have it.
    class ColIterator {
    public:
        ColIterator() = default;
        ColIterator(const cRZSparseMatrix* matrix, std::int32_t col, bool atEnd) : matrix_(matrix), col_(col)
        {
            if (!matrix_) return;
            row_ = atEnd ? matrix_->rows_.end() : matrix_->rows_.begin();
            if (!atEnd) SkipRowsWithoutCol();
        }
        MatrixUserCell<T> operator*() const { return MatrixUserCell<T>{col_, row_->first, row_->second.at(col_)}; }
        ColIterator& operator++()
        {
            if (!matrix_) return *this;
            ++row_;
            SkipRowsWithoutCol();
            return *this;
        }
        bool operator==(const ColIterator& other) const { return AtEnd() == other.AtEnd(); }
        bool operator!=(const ColIterator& other) const { return !(*this == other); }

    private:
        bool AtEnd() const { return !matrix_ || row_ == matrix_->rows_.end(); }
        void SkipRowsWithoutCol()
        {
            while (row_ != matrix_->rows_.end() && row_->second.find(col_) == row_->second.end()) ++row_;
        }
        const cRZSparseMatrix* matrix_ = nullptr;
        std::int32_t col_ = 0;
        typename std::map<std::int32_t, RowMap>::const_iterator row_;
    };

    Iterator begin() const { return Iterator(this, false); }
    Iterator end() const { return Iterator(this, true); }
    ColIterator ColBegin(std::int32_t col) const { return ColIterator(this, col, false); }
    ColIterator ColEnd(std::int32_t col) const { return ColIterator(this, col, true); }

private:
    std::map<std::int32_t, RowMap> rows_;   // +0x00 (row -> column -> value)
    std::int32_t count_ = 0;                // +0x18
    std::int32_t minRow_ = kNoLimit;        // +0x1c
    std::int32_t maxRow_ = kLimitMax;       // +0x20
    std::int32_t minCol_ = kNoLimit;        // +0x24
    std::int32_t maxCol_ = kLimitMax;       // +0x28
    bool initialised_ = false;              // +0x2c
};

}   // namespace rz
