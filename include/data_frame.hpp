#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <variant>
#include <stdexcept>
#include <variant>

namespace framework {

    using data_value = std::variant<int, double, std::string, bool>;

    // Base column interface
    struct IColumn {
        virtual ~IColumn() = default;
        virtual data_value get(size_t row) const = 0;
        virtual void set(size_t row, const data_value& val) = 0;
        virtual void push_back(const data_value& val) = 0;
        virtual size_t size() const = 0;
    };

    // Typed column
    template<typename T>
    struct data_column : IColumn {
        std::string name;
        std::vector<T> data;

        data_column(const std::string& n) : name(n) {}

        data_value get(size_t row) const override { return data.at(row); }
        void set(size_t row, const data_value& val) override { data.at(row) = std::get<T>(val); }
        void push_back(const data_value& val) override { data.push_back(std::get<T>(val)); }
        size_t size() const override { return data.size(); }
    };

    // Proxy to access a row
    class row_view {
        std::vector<std::shared_ptr<IColumn>>& columns_;
        std::unordered_map<std::string, size_t>& col_map_;
        size_t row_;
    public:
        row_view(std::vector<std::shared_ptr<IColumn>>& cols,
            std::unordered_map<std::string, size_t>& cmap,
            size_t row) : columns_(cols), col_map_(cmap), row_(row) {
        }

        data_value operator[](const std::string& col_name) const {
            return columns_[col_map_.at(col_name)]->get(row_);
        }

        void set(const std::string& col_name, const data_value& val) {
            columns_[col_map_.at(col_name)]->set(row_, val);
        }

        // Optional: operator[] assignment style
        struct Proxy {
            IColumn* col;
            size_t row;
            Proxy& operator=(const data_value& val) { col->set(row, val); return *this; }
            operator data_value() const { return col->get(row); }
        };

        Proxy operator[](const std::string& col_name) {
            return Proxy{ columns_[col_map_.at(col_name)].get(), row_ };
        }
    };

    class data_frame {
        std::vector<std::shared_ptr<IColumn>> columns_;
        std::unordered_map<std::string, size_t> columns_map_;
    public:
        template<typename T>
        void addColumn(const std::string& name) {
            if (columns_map_.contains(name)) throw std::runtime_error("Column exists");
            columns_map_[name] = columns_.size();
            columns_.push_back(std::make_shared<data_column<T>>(name));
        }

        void addRow(const std::vector<data_value>& row) {
            if (row.size() != columns_.size()) throw std::runtime_error("Row size mismatch");
            for (size_t i = 0; i < row.size(); ++i)
                columns_[i]->push_back(row[i]);
        }

        row_view operator[](size_t row_index) {
            return row_view(columns_, columns_map_, row_index);
        }

        size_t rowCount() const { return columns_.empty() ? 0 : columns_[0]->size(); }
        size_t columnCount() const { return columns_.size(); }
    };

} // namespace framework
