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

#include <cif++/v2/forward_decl.hpp>

#include <cif++/v2/condition.hpp>
#include <cif++/v2/iterator.hpp>
#include <cif++/v2/row.hpp>
#include <cif++/v2/validate.hpp>

// TODO: implement all of:
// https://en.cppreference.com/w/cpp/named_req/Container
// https://en.cppreference.com/w/cpp/named_req/SequenceContainer
// and more?


namespace cif::v2
{

// --------------------------------------------------------------------

class category
{
  public:
	friend class row_handle;

	template <typename, typename...>
	friend class iterator_impl;

	using value_type = row_handle;
	using reference = value_type;
	using const_reference = const value_type;
	using iterator = iterator_impl<category>;
	using const_iterator = iterator_impl<const category>;

	category() = default;

	category(std::string_view name);

	category(const category &rhs);

	category(category &&rhs);

	category &operator=(const category &rhs);

	category &operator=(category &&rhs);

	~category();

	// --------------------------------------------------------------------

	const std::string &name() const { return m_name; }

	iset fields() const;

	std::set<uint16_t> key_field_indices() const;

	void set_validator(const validator *v, datablock &db);
	void update_links(datablock &db);

	const validator *get_validator() const { return m_validator; }
	const category_validator *get_cat_validator() const { return m_cat_validator; }

	bool is_valid() const;

	// --------------------------------------------------------------------

	reference front()
	{
		return {*this, *m_head};
	}

	const_reference front() const
	{
		return {const_cast<category &>(*this), const_cast<row &>(*m_head)};
	}

	reference back()
	{
		return {*this, *m_tail};
	}

	const_reference back() const
	{
		return {const_cast<category &>(*this), const_cast<row &>(*m_tail)};
	}

	iterator begin()
	{
		return {*this, m_head};
	}

	iterator end()
	{
		return {*this, nullptr};
	}

	const_iterator begin() const
	{
		return {*this, m_head};
	}

	const_iterator end() const
	{
		return {*this, nullptr};
	}

	const_iterator cbegin() const
	{
		return {*this, m_head};
	}

	const_iterator cend() const
	{
		return {*this, nullptr};
	}

	size_t size() const
	{
		return std::distance(cbegin(), cend());
	}

	bool empty() const
	{
		return m_head == nullptr;
	}

	// --------------------------------------------------------------------

	template <typename... Ts, typename... Ns>
	iterator_proxy<const category, Ts...> rows(Ns... names) const
	{
		static_assert(sizeof...(Ts) == sizeof...(Ns), "The number of column titles should be equal to the number of types to return");
		return iterator_proxy<const category, Ts...>(*this, begin(), {names...});
	}

	template <typename... Ts, typename... Ns>
	iterator_proxy<category, Ts...> rows(Ns... names)
	{
		static_assert(sizeof...(Ts) == sizeof...(Ns), "The number of column titles should be equal to the number of types to return");
		return iterator_proxy<category, Ts...>(*this, begin(), {names...});
	}

	// --------------------------------------------------------------------

	conditional_iterator_proxy<category> find(condition &&cond)
	{
		return find(begin(), std::forward<condition>(cond));
	}

	conditional_iterator_proxy<category> find(iterator pos, condition &&cond)
	{
		return {*this, pos, std::forward<condition>(cond)};
	}

	conditional_iterator_proxy<const category> find(condition &&cond) const
	{
		return find(cbegin(), std::forward<condition>(cond));
	}

	conditional_iterator_proxy<const category> find(const_iterator pos, condition &&cond) const
	{
		return conditional_iterator_proxy<const category>{*this, pos, std::forward<condition>(cond)};
	}

	template <typename... Ts, typename... Ns>
	conditional_iterator_proxy<category, Ts...> find(condition &&cond, Ns... names)
	{
		static_assert(sizeof...(Ts) == sizeof...(Ns), "The number of column titles should be equal to the number of types to return");
		return find<Ts...>(cbegin(), std::forward<condition>(cond), std::forward<Ns>(names)...);
	}

	template <typename... Ts, typename... Ns>
	conditional_iterator_proxy<const category, Ts...> find(condition &&cond, Ns... names) const
	{
		static_assert(sizeof...(Ts) == sizeof...(Ns), "The number of column titles should be equal to the number of types to return");
		return find<Ts...>(cbegin(), std::forward<condition>(cond), std::forward<Ns>(names)...);
	}

	template <typename... Ts, typename... Ns>
	conditional_iterator_proxy<category, Ts...> find(const_iterator pos, condition &&cond, Ns... names)
	{
		static_assert(sizeof...(Ts) == sizeof...(Ns), "The number of column titles should be equal to the number of types to return");
		return {*this, pos, std::forward<condition>(cond), std::forward<Ns>(names)...};
	}

	template <typename... Ts, typename... Ns>
	conditional_iterator_proxy<const category, Ts...> find(const_iterator pos, condition &&cond, Ns... names) const
	{
		static_assert(sizeof...(Ts) == sizeof...(Ns), "The number of column titles should be equal to the number of types to return");
		return {*this, pos, std::forward<condition>(cond), std::forward<Ns>(names)...};
	}

	// --------------------------------------------------------------------
	// if you only expect a single row

	row_handle find1(condition &&cond)
	{
		return find1(begin(), std::forward<condition>(cond));
	}

	row_handle find1(iterator pos, condition &&cond)
	{
		auto h = find(pos, std::forward<condition>(cond));

		if (h.empty())
			throw std::runtime_error("No hits found");

		if (h.size() != 1)
			throw std::runtime_error("Hit not unique");

		return *h.begin();
	}

	const row_handle find1(condition &&cond) const
	{
		return find1(cbegin(), std::forward<condition>(cond));
	}

	const row_handle find1(const_iterator pos, condition &&cond) const
	{
		auto h = find(pos, std::forward<condition>(cond));

		if (h.empty())
			throw std::runtime_error("No hits found");

		if (h.size() != 1)
			throw std::runtime_error("Hit not unique");

		return *h.begin();
	}

	template <typename T>
	T find1(condition &&cond, const char *column) const
	{
		return find1<T>(cbegin(), std::forward<condition>(cond), column);
	}

	template <typename T>
	T find1(const_iterator pos, condition &&cond, const char *column) const
	{
		auto h = find<T>(pos, std::forward<condition>(cond), column);

		if (h.empty())
			throw std::runtime_error("No hits found");

		if (h.size() != 1)
			throw std::runtime_error("Hit not unique");

		return std::get<0>(*h.begin());
	}

	template <typename... Ts, typename... Cs, typename U = std::enable_if_t<sizeof...(Ts) != 1>>
	std::tuple<Ts...> find1(condition &&cond, Cs... columns) const
	{
		static_assert(sizeof...(Ts) == sizeof...(Cs), "The number of column titles should be equal to the number of types to return");
		// static_assert(std::is_same_v<Cs, const char*>..., "The column names should be const char");
		return find1<Ts...>(cbegin(), std::forward<condition>(cond), std::forward<Cs>(columns)...);
	}

	template <typename... Ts, typename... Cs, typename U = std::enable_if_t<sizeof...(Ts) != 1>>
	std::tuple<Ts...> find1(const_iterator pos, condition &&cond, Cs... columns) const
	{
		static_assert(sizeof...(Ts) == sizeof...(Cs), "The number of column titles should be equal to the number of types to return");
		auto h = find<Ts...>(pos, std::forward<condition>(cond), std::forward<Cs>(columns)...);

		if (h.empty())
			throw std::runtime_error("No hits found");

		if (h.size() != 1)
			throw std::runtime_error("Hit not unique");

		return *h.begin();
	}

	bool exists(condition &&cond) const
	{
		bool result = false;

		cond.prepare(*this);

		for (auto r : *this)
		{
			if (cond(r))
			{
				result = true;
				break;
			}
		}

		return result;
	}

	// --------------------------------------------------------------------
	
	bool has_children(row_handle r) const;
	bool has_parents(row_handle r) const;

	std::vector<row_handle> get_children(row_handle r, category &childCat);
	// std::vector<row_handle> getChildren(row_handle r, const char *childCat);

	std::vector<row_handle> get_parents(row_handle r, category &parentCat);
	// std::vector<row_handle> getParents(row_handle r, const char *parentCat);

	std::vector<row_handle> get_linked(row_handle r, category &cat);
	// std::vector<row_handle> getLinked(row_handle r, const char *cat);

	// --------------------------------------------------------------------

	// void insert(const_iterator pos, const row_initializer &row)
	// {
	// 	insert_impl(pos, row);
	// }

	// void insert(const_iterator pos, row_initializer &&row)
	// {
	// 	insert_impl(pos, std::move(row));
	// }

	iterator erase(iterator pos);
	size_t erase(condition &&cond);
	size_t erase(condition &&cond, std::function<void(row_handle)> &&visit);

	iterator emplace(row_initializer &&ri)
	{
		return this->emplace(ri.m_items.begin(), ri.m_items.end());
	}

	template <typename ItemIter>
	iterator emplace(ItemIter b, ItemIter e)
	{
		row *r = this->create_row();

		try
		{
			for (auto i = b; i != e; ++i)
			{
				item_value *new_item = this->create_item(*i);
				r->append(new_item);
			}
		}
		catch (...)
		{
			if (r != nullptr)
				this->delete_row(r);
			throw;
		}

		return insert_impl(cend(), r);
	}

	void clear()
	{
		auto i = m_head;
		while (i != nullptr)
		{
			auto t = i;
			i = i->m_next;
			delete_row(t);
		}

		m_head = m_tail = nullptr;
	}

	// --------------------------------------------------------------------
	/// \brief Return the index number for \a column_name

	uint16_t get_column_ix(std::string_view column_name) const
	{
		uint16_t result;

		for (result = 0; result < m_columns.size(); ++result)
		{
			if (iequals(column_name, m_columns[result].m_name))
				break;
		}

		if (VERBOSE > 0 and result == m_columns.size() and m_cat_validator != nullptr) // validate the name, if it is known at all (since it was not found)
		{
			auto iv = m_cat_validator->get_validator_for_item(column_name);
			if (iv == nullptr)
				std::cerr << "Invalid name used '" << column_name << "' is not a known column in " + m_name << std::endl;
		}

		return result;
	}

	std::string_view get_column_name(uint16_t ix) const
	{
		if (ix >= m_columns.size())
			throw std::out_of_range("column index is out of range");
		
		return m_columns[ix].m_name;
	}

	uint16_t add_column(std::string_view column_name)
	{
		using namespace std::literals;

		size_t result = get_column_ix(column_name);

		if (result == m_columns.size())
		{
			const item_validator *item_validator = nullptr;

			if (m_cat_validator != nullptr)
			{
				item_validator = m_cat_validator->get_validator_for_item(column_name);
				if (item_validator == nullptr)
					m_validator->report_error("tag " + std::string(column_name) + " not allowed in category " + m_name, false);
			}

			m_columns.emplace_back(column_name, item_validator);
		}

		return result;
	}

  private:
	void update_value(row *row, size_t column, std::string_view value, bool updateLinked, bool validate = true);

  private:
	bool is_orphan(row_handle r) const;
	void erase_orphans(condition &&cond);

	using allocator_type = std::allocator<void>;

	constexpr allocator_type get_allocator() const
	{
		return {};
	}

	using char_allocator_type = typename std::allocator_traits<allocator_type>::template rebind_alloc<char>;
	using char_allocator_traits = std::allocator_traits<char_allocator_type>;

	using item_allocator_type = typename std::allocator_traits<allocator_type>::template rebind_alloc<item_value>;
	using item_allocator_traits = std::allocator_traits<item_allocator_type>;

	item_allocator_traits::pointer get_item()
	{
		item_allocator_type ia(get_allocator());
		return item_allocator_traits::allocate(ia, 1);
	}

	item_value *create_item(uint16_t column_ix, std::string_view text)
	{
		size_t text_length = text.length();

		if (text_length + 1 > std::numeric_limits<uint16_t>::max())
			throw std::runtime_error("libcifpp does not support string lengths longer than " + std::to_string(std::numeric_limits<uint16_t>::max()) + " bytes");

		auto p = this->get_item();
		item_allocator_type ia(get_allocator());
		item_allocator_traits::construct(ia, p, column_ix, static_cast<uint16_t>(text_length));

		char_allocator_type ca(get_allocator());

		char *data;
		if (text_length >= item_value::kBufferSize)
			data = p->m_data = char_allocator_traits::allocate(ca, text_length + 1);
		else
			data = p->m_local_data;

		std::copy(text.begin(), text.end(), data);
		data[text_length] = 0;

		return p;
	}

	item_value *create_item(const item &i)
	{
		uint16_t ix = add_column(i.name());
		return create_item(ix, i.value());
	}

	void delete_item(item_value *iv)
	{
		if (iv->m_length >= item_value::kBufferSize)
		{
			char_allocator_type ca(get_allocator());
			char_allocator_traits::deallocate(ca, iv->m_data, iv->m_length + 1);
		}

		item_allocator_type ia(get_allocator());
		item_allocator_traits::destroy(ia, iv);
		item_allocator_traits::deallocate(ia, iv, 1);
	}

	using row_allocator_type = typename std::allocator_traits<allocator_type>::template rebind_alloc<row>;
	using row_allocator_traits = std::allocator_traits<row_allocator_type>;

	row_allocator_traits::pointer get_row()
	{
		row_allocator_type ra(get_allocator());
		return row_allocator_traits::allocate(ra, 1);
	}

	row *create_row()
	{
		auto p = this->get_row();
		row_allocator_type ra(get_allocator());
		row_allocator_traits::construct(ra, p);
		return p;
	}

	row *clone_row(const row &r)
	{
		row *result = create_row();

		try
		{
			for (auto i = r.m_head; i != nullptr; i = i->m_next)
			{
				item_value *v = create_item(i->m_column_ix, i->text());
				result->append(v);
			}
		}
		catch (...)
		{
			delete_row(result);
			throw;
		}

		return result;
	}

	void delete_row(row *r)
	{
		if (r != nullptr)
		{
			auto i = r->m_head;
			while (i != nullptr)
			{
				auto t = i;
				i = i->m_next;
				delete_item(t);
			}

			row_allocator_type ra(get_allocator());
			row_allocator_traits::destroy(ra, r);
			row_allocator_traits::deallocate(ra, r, 1);
		}
	}

	struct item_column
	{
		std::string m_name;
		const item_validator *m_validator;

		item_column(std::string_view name, const item_validator *validator)
			: m_name(name)
			, m_validator(validator)
		{
		}
	};

	struct link
	{
		link(category *linked, const link_validator *v)
			: linked(linked)
			, v(v)
		{
		}

		category *linked;
		const link_validator *v;
	};

	// proxy methods for every insertion
	iterator insert_impl(const_iterator pos, row *n);
	iterator erase_impl(const_iterator pos);

	std::string m_name;
	std::vector<item_column> m_columns;
	const validator *m_validator = nullptr;
	const category_validator *m_cat_validator = nullptr;
	std::vector<link> m_parent_links, m_child_links;
	bool m_cascade = true;
	class category_index* m_index = nullptr;
	row *m_head = nullptr, *m_tail = nullptr;
};

} // namespace cif::v2