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

#include "cif++/datablock.hpp"

namespace cif
{

datablock::datablock(const datablock &db)
	: std::list<category>(db)
	, m_name(db.m_name)
	, m_validator(db.m_validator)
{
	for (auto &cat : *this)
		cat.update_links(*this);
}

datablock &datablock::operator=(const datablock &db)
{
	if (this != &db)
	{
		std::list<category>::operator=(db);
		m_name = db.m_name;
		m_validator = db.m_validator;

		for (auto &cat : *this)
			cat.update_links(*this);
	}

	return *this;
}

void datablock::set_validator(const validator *v)
{
	m_validator = v;

	try
	{
		for (auto &cat : *this)
			cat.set_validator(v, *this);
	}
	catch (const std::exception &)
	{
		throw_with_nested(std::runtime_error("Error while setting validator in datablock " + m_name));
	}
}

const validator *datablock::get_validator() const
{
	return m_validator;
}

bool datablock::is_valid() const
{
	if (m_validator == nullptr)
		throw std::runtime_error("Validator not specified");

	bool result = true;
	for (auto &cat : *this)
		result = cat.is_valid() and result;

	return result;
}

bool datablock::validate_links() const
{
	bool result = true;

	for (auto &cat : *this)
		result = cat.validate_links() and result;
	
	return result;
}

// --------------------------------------------------------------------

category &datablock::operator[](std::string_view name)
{
	auto i = std::find_if(begin(), end(), [name](const category &c)
		{ return iequals(c.name(), name); });

	if (i != end())
		return *i;

	auto &cat = emplace_back(name);

	if (m_validator)
		cat.set_validator(m_validator, *this);

	return back();
}

const category &datablock::operator[](std::string_view name) const
{
	static const category s_empty;
	auto i = std::find_if(begin(), end(), [name](const category &c)
		{ return iequals(c.name(), name); });
	return i == end() ? s_empty : *i;
}

category *datablock::get(std::string_view name)
{
	auto i = std::find_if(begin(), end(), [name](const category &c)
		{ return iequals(c.name(), name); });
	return i == end() ? nullptr : &*i;
}

const category *datablock::get(std::string_view name) const
{
	return const_cast<datablock *>(this)->get(name);
}

std::tuple<datablock::iterator, bool> datablock::emplace(std::string_view name)
{
	bool is_new = true;

	auto i = begin();
	while (i != end())
	{
		if (iequals(name, i->name()))
		{
			is_new = false;

			if (i != begin())
			{
				auto n = std::next(i);
				splice(begin(), *this, i, n);
			}

			break;
		}

		++i;
	}

	if (is_new)
	{
		auto &c = emplace_front(name);
		c.set_validator(m_validator, *this);
	}

	return std::make_tuple(begin(), is_new);
}

std::vector<std::string> datablock::get_tag_order() const
{
	std::vector<std::string> result;

	// for entry and audit_conform on top

	auto ci = find_if(begin(), end(), [](const category &cat) { return cat.name() == "entry"; });
	if (ci != end())
	{
		auto cto = ci->get_tag_order();
		result.insert(result.end(), cto.begin(), cto.end());
	}

	ci = find_if(begin(), end(), [](const category &cat) { return cat.name() == "audit_conform"; });
	if (ci != end())
	{
		auto cto = ci->get_tag_order();
		result.insert(result.end(), cto.begin(), cto.end());
	}

	for (auto &cat : *this)
	{
		if (cat.name() == "entry" or cat.name() == "audit_conform")
			continue;
		auto cto = cat.get_tag_order();
		result.insert(result.end(), cto.begin(), cto.end());
	}

	return result;
}

void datablock::write(std::ostream &os) const
{
	os << "data_" << m_name << '\n'
	   << "# \n";

	// mmcif support, sort of. First write the 'entry' Category
	// and if it exists, _AND_ we have a Validator, write out the
	// audit_conform record.

	for (auto &cat : *this)
	{
		if (cat.name() != "entry")
			continue;

		cat.write(os);

		break;
	}

	// If the dictionary declares an audit_conform category, put it in,
	// but only if it does not exist already!
	if (get("audit_conform"))
		get("audit_conform")->write(os);
	else if (m_validator != nullptr and m_validator->get_validator_for_category("audit_conform") != nullptr)
	{
		category auditConform("audit_conform");
		auditConform.emplace({
			{"dict_name", m_validator->name()},
			{"dict_version", m_validator->version()}});
		auditConform.write(os);
	}

	for (auto &cat : *this)
	{
		if (cat.name() != "entry" and cat.name() != "audit_conform")
			cat.write(os);
	}
}

void datablock::write(std::ostream &os, const std::vector<std::string> &tag_order)
{
	os << "data_" << m_name << '\n'
	   << "# \n";

	std::vector<std::string> cat_order;
	for (auto &o : tag_order)
	{
		std::string cat_name, item_name;
		std::tie(cat_name, item_name) = split_tag_name(o);
		if (find_if(cat_order.rbegin(), cat_order.rend(), [cat_name](const std::string &s) -> bool
				{ return iequals(cat_name, s); }) == cat_order.rend())
			cat_order.push_back(cat_name);
	}

	for (auto &c : cat_order)
	{
		auto cat = get(c);
		if (cat == nullptr)
			continue;

		std::vector<std::string> items;
		for (auto &o : tag_order)
		{
			std::string cat_name, item_name;
			std::tie(cat_name, item_name) = split_tag_name(o);

			if (cat_name == c)
				items.push_back(item_name);
		}

		cat->write(os, items);
	}

	// for any Category we missed in the catOrder
	for (auto &cat : *this)
	{
		if (find_if(cat_order.begin(), cat_order.end(), [&](const std::string &s) -> bool
				{ return iequals(cat.name(), s); }) != cat_order.end())
			continue;

		cat.write(os);
	}
}

bool datablock::operator==(const datablock &rhs) const
{
	auto &dbA = *this;
	auto &dbB = rhs;

	std::vector<std::string> catA, catB;

	for (auto &cat : dbA)
	{
		if (not cat.empty())
			catA.push_back(cat.name());
	}
	std::sort(catA.begin(), catA.end());

	for (auto &cat : dbB)
	{
		if (not cat.empty())
			catB.push_back(cat.name());
	}
	std::sort(catB.begin(), catB.end());

	// loop over categories twice, to group output
	// First iteration is to list missing categories.

	std::vector<std::string> missingA, missingB;

	auto catA_i = catA.begin(), catB_i = catB.begin();

	while (catA_i != catA.end() and catB_i != catB.end())
	{
		if (not iequals(*catA_i, *catB_i))
			return false;

		++catA_i, ++catB_i;
	}

	if (catA_i != catA.end() or catB_i != catB.end())
		return false;

	// Second loop, now compare category values
	catA_i = catA.begin(), catB_i = catB.begin();

	while (catA_i != catA.end() and catB_i != catB.end())
	{
		std::string nA = *catA_i;
		to_lower(nA);

		std::string nB = *catB_i;
		to_lower(nB);

		int d = nA.compare(nB);
		if (d > 0)
			++catB_i;
		else if (d < 0)
			++catA_i;
		else
		{
			if (not (*dbA.get(*catA_i) == *dbB.get(*catB_i)))
				return false;
			++catA_i;
			++catB_i;
		}
	}

	return true;
}

} // namespace cif::cif