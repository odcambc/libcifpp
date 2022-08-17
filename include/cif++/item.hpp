/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2022 NKI/AVL, Netherlands Cancer Institute
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <charconv>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>

#include <cif++/text.hpp>

#include <cif++/forward_decl.hpp>

namespace cif
{

extern int VERBOSE;

// --------------------------------------------------------------------
/// \brief item is a transient class that is used to pass data into rows
///        but it also takes care of formatting data
class item
{
  public:
	item() = default;

	item(std::string_view name, char value)
		: m_name(name)
		, m_value(m_buffer, 1)
	{
		m_buffer[0] = value;
		m_buffer[1] = 0;
	}

	template <typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
	item(std::string_view name, const T &value, int precision)
		: m_name(name)
	{
		auto r = cif::to_chars(m_buffer, m_buffer + sizeof(m_buffer) - 1, value, cif::chars_format::fixed, precision);
		if (r.ec != std::errc())
			throw std::runtime_error("Could not format number");

		assert(r.ptr >= m_buffer and r.ptr < m_buffer + sizeof(m_buffer));
		*r.ptr = 0;
		m_value = std::string_view(m_buffer, r.ptr - m_buffer);
	}

	template <typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
	item(const std::string_view name, const T &value)
		: m_name(name)
	{
		auto r = cif::to_chars(m_buffer, m_buffer + sizeof(m_buffer) - 1, value, cif::chars_format::general);
		if (r.ec != std::errc())
			throw std::runtime_error("Could not format number");

		assert(r.ptr >= m_buffer and r.ptr < m_buffer + sizeof(m_buffer));
		*r.ptr = 0;
		m_value = std::string_view(m_buffer, r.ptr - m_buffer);
	}

	template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
	item(const std::string_view name, const T &value)
		: m_name(name)
	{
		auto r = std::to_chars(m_buffer, m_buffer + sizeof(m_buffer) - 1, value);
		if (r.ec != std::errc())
			throw std::runtime_error("Could not format number");

		assert(r.ptr >= m_buffer and r.ptr < m_buffer + sizeof(m_buffer));
		*r.ptr = 0;
		m_value = std::string_view(m_buffer, r.ptr - m_buffer);
	}

	item(const std::string_view name, const std::string_view value)
		: m_name(name)
		, m_value(value)
	{
	}

	item(const item &rhs) = default;

	item(item &&rhs) noexcept = default;

	item &operator=(const item &rhs) = default;

	item &operator=(item &&rhs) noexcept = default;

	std::string_view name() const { return m_name; }
	std::string_view value() const { return m_value; }

	void value(const std::string &v) { m_value = v; }

	/// \brief empty means either null or unknown
	bool empty() const { return m_value.empty(); }

	/// \brief returns true if the field contains '.'
	bool is_null() const { return m_value == "."; }

	/// \brief returns true if the field contains '?'
	bool is_unknown() const { return m_value == "?"; }

	size_t length() const { return m_value.length(); }
	// const char *c_str() const { return m_value.c_str(); }

  private:
	std::string_view m_name;
	std::string_view m_value;
	char m_buffer[64]; // TODO: optimize this magic number, might be too large
};

// --------------------------------------------------------------------
/// \brief the internal storage for items in a category
///
/// Internal storage, strictly forward linked list with minimal space
/// requirements. Strings of size 7 or shorter are stored internally.
/// Typically, more than 99% of the strings in an mmCIF file are less
/// than 8 bytes in length.

struct item_value
{
	item_value(uint16_t column_ix, uint16_t length)
		: m_next(nullptr)
		, m_column_ix(column_ix)
		, m_length(length)
	{
	}

	item_value() = delete;
	item_value(const item_value &) = delete;
	item_value &operator=(const item_value &) = delete;

	item_value *m_next;
	uint16_t m_column_ix;
	uint16_t m_length;
	union
	{
		char m_local_data[8];
		char *m_data;
	};

	static constexpr size_t kBufferSize = sizeof(m_local_data);

	std::string_view text() const
	{
		return {m_length >= kBufferSize ? m_data : m_local_data, m_length};
	}

	const char *c_str() const
	{
		return m_length >= kBufferSize ? m_data : m_local_data;
	}
};

static_assert(sizeof(item_value) == 24, "sizeof(item_value) should be 24 bytes");

// --------------------------------------------------------------------
// Transient object to access stored data

struct item_handle
{
  public:
	// conversion helper class
	template <typename T, typename = void>
	struct item_value_as;

	template <typename T>
	item_handle &operator=(const T &value)
	{
		item v{"", value};
		assign_value(v);
		return *this;
	}

	template <typename... Ts>
	void os(const Ts &...v)
	{
		std::ostringstream ss;
		((ss << v), ...);
		this->operator=(ss.str());
	}

	void swap(item_handle &b);

	template <typename T = std::string>
	auto as() const -> T
	{
		using value_type = std::remove_cv_t<std::remove_reference_t<T>>;
		return item_value_as<value_type>::convert(*this);
	}

	template <typename T>
	auto value_or(const T &dv) const
	{
		return empty() ? dv : this->as<T>();
	}

	template <typename T>
	int compare(const T &value, bool icase = true) const
	{
		return item_value_as<T>::compare(*this, value, icase);
	}

	template <typename T>
	bool operator==(const T &value) const
	{
		// TODO: icase or not icase?
		return item_value_as<T>::compare(*this, value, true) == 0;
	}

	// We may not have C++20 yet...

	template <typename T>
	bool operator!=(const T &value) const
	{
		// TODO: icase or not icase?
		return item_value_as<T>::compare(*this, value, true) != 0;
	}

	// empty means either null or unknown
	bool empty() const
	{
		auto txt = text();
		return txt.empty() or (txt.length() == 1 and (txt.front() == '.' or txt.front() == '?'));
	}

	explicit operator bool() const { return not empty(); }

	// is_null means the field contains '.'
	bool is_null() const
	{
		auto txt = text();
		return txt.length() == 1 and txt.front() == '.';
	}

	// is_unknown means the field contains '?'
	bool is_unknown() const
	{
		auto txt = text();
		return txt.length() == 1 and txt.front() == '?';
	}

	// const char *c_str() const
	// {
	// 	for (auto iv = m_row_handle.m_head; iv != nullptr; iv = iv->m_next)
	// 	{
	// 		if (iv->m_column_ix == m_column)
	// 			return iv->m_text;
	// 	}

	// 	return s_empty_result;
	// }

	std::string_view text() const;

	// bool operator!=(const std::string &s) const { return s != c_str(); }
	// bool operator==(const std::string &s) const { return s == c_str(); }

	item_handle(uint16_t column, row_handle &row)
		: m_column(column)
		, m_row_handle(row)
	{
	}

  private:
	uint16_t m_column;
	row_handle &m_row_handle;

	void assign_value(const item &value);

	static constexpr const char *s_empty_result = "";
};

// So sad that the gcc implementation of from_chars does not support floats yet...

template <typename T>
struct item_handle::item_value_as<T, std::enable_if_t<std::is_arithmetic_v<T> and not std::is_same_v<T, bool>>>
{
	using value_type = std::remove_reference_t<std::remove_cv_t<T>>;

	static value_type convert(const item_handle &ref)
	{
		auto txt = ref.text();

		value_type result = {};

		std::from_chars_result r;

		if constexpr (std::is_floating_point_v<T>)
			r = cif::from_chars(txt.data(), txt.data() + txt.size(), result);
		else
			r = std::from_chars(txt.data(), txt.data() + txt.size(), result);

		if (r.ec != std::errc())
		{
			result = {};
			if (cif::VERBOSE)
			{
				if (r.ec == std::errc::invalid_argument)
					std::cerr << "Attempt to convert " << std::quoted(txt) << " into a number" << std::endl;
				else if (r.ec == std::errc::result_out_of_range)
					std::cerr << "Conversion of " << std::quoted(txt) << " into a type that is too small" << std::endl;
			}
		}

		return result;
	}

	static int compare(const item_handle &ref, const T &value, bool icase)
	{
		int result = 0;

		auto txt = ref.text();

		if (txt.empty())
			result = 1;
		else
		{
			value_type v = {};

			std::from_chars_result r;

			if constexpr (std::is_floating_point_v<T>)
				r = cif::from_chars(txt.data(), txt.data() + txt.size(), v);
			else
				r = std::from_chars(txt.data(), txt.data() + txt.size(), v);

			if (r.ec != std::errc())
			{
				if (cif::VERBOSE)
				{
					if (r.ec == std::errc::invalid_argument)
						std::cerr << "Attempt to convert " << std::quoted(txt) << " into a number" << std::endl;
					else if (r.ec == std::errc::result_out_of_range)
						std::cerr << "Conversion of " << std::quoted(txt) << " into a type that is too small" << std::endl;
				}
				result = 1;
			}
			else if (v < value)
				result = -1;
			else if (v > value)
				result = 1;
		}

		return result;
	}
};

template <typename T>
struct item_handle::item_value_as<std::optional<T>>
{
	static std::optional<T> convert(const item_handle &ref)
	{
		std::optional<T> result;
		if (ref)
			result = ref.as<T>();
		return result;
	}

	static int compare(const item_handle &ref, std::optional<T> value, bool icase)
	{
		if (ref.empty() and not value)
			return 0;

		if (ref.empty())
			return -1;
		else if (not value)
			return 1;
		else
			return ref.compare(*value, icase);
	}
};

template <typename T>
struct item_handle::item_value_as<T, std::enable_if_t<std::is_same_v<T, bool>>>
{
	static bool convert(const item_handle &ref)
	{
		bool result = false;
		if (not ref.empty())
			result = iequals(ref.text(), "y");
		return result;
	}

	static int compare(const item_handle &ref, bool value, bool icase)
	{
		bool rv = convert(ref);
		return value && rv ? 0
		                   : (rv < value ? -1 : 1);
	}
};

template <size_t N>
struct item_handle::item_value_as<char[N]>
{
	// static std::string_view convert(const item_handle &ref)
	// {
	// 	return ref.text();
	// }

	static int compare(const item_handle &ref, const char (&value)[N], bool icase)
	{
		return icase ? cif::icompare(ref.text(), value) : ref.text().compare(value);
	}
};

template <typename T>
struct item_handle::item_value_as<T, std::enable_if_t<std::is_same_v<T, const char *>>>
{
	// static std::string_view convert(const item_handle &ref)
	// {
	// 	return ref.text();
	// }

	static int compare(const item_handle &ref, const char *value, bool icase)
	{
		return icase ? cif::icompare(ref.text(), value) : ref.text().compare(value);
	}
};

template <typename T>
struct item_handle::item_value_as<T, std::enable_if_t<std::is_same_v<T, std::string_view>>>
{
	// static std::string_view convert(const item_handle &ref)
	// {
	// 	return ref.text();
	// }

	static int compare(const item_handle &ref, const std::string_view &value, bool icase)
	{
		return icase ? cif::icompare(ref.text(), value) : ref.text().compare(value);
	}
};

template <typename T>
struct item_handle::item_value_as<T, std::enable_if_t<std::is_same_v<T, std::string>>>
{
	static std::string convert(const item_handle &ref)
	{
		if (ref.empty())
			return {};
		return {ref.text().data(), ref.text().size()};
	}

	static int compare(const item_handle &ref, const std::string &value, bool icase)
	{
		return icase ? cif::icompare(ref.text(), value) : ref.text().compare(value);
	}
};

} // namespace cif
