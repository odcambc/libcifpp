/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2020 NKI/AVL, Netherlands Cancer Institute
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

#include "cif++/utilities.hpp"
#include "cif++/forward_decl.hpp"
#include "cif++/parser.hpp"
#include "cif++/file.hpp"

#include <cassert>
#include <iostream>
#include <map>
#include <regex>
#include <stack>

namespace cif
{

// --------------------------------------------------------------------

sac_parser::sac_parser(std::istream &is, bool init)
	: m_source(*is.rdbuf())
{
	if (is.rdbuf() == nullptr)
		throw std::runtime_error("Attempt to read from uninitialised stream");

	m_validate = true;
	m_line_nr = 1;
	m_bol = true;

	if (init)
		m_lookahead = get_next_token();
}

// get_next_char takes a char from the buffer, or if it is empty
// from the istream. This function also does carriage/linefeed
// translation.
int sac_parser::get_next_char()
{
	int result = std::char_traits<char>::eof();

	if (m_buffer.empty())
		result = m_source.sbumpc();
	else
	{
		result = m_buffer.back();
		m_buffer.pop_back();
	}

	// very simple CR/LF translation into LF
	if (result == '\r')
	{
		int lookahead = m_source.sbumpc();
		if (lookahead != '\n')
			m_buffer.push_back(lookahead);
		result = '\n';
	}

	if (result == std::char_traits<char>::eof())
		m_token_value.push_back(0);
	else
		m_token_value.push_back(std::char_traits<char>::to_char_type(result));

	if (result == '\n')
		++m_line_nr;

	if (VERBOSE >= 6)
	{
		std::cerr << "get_next_char => ";
		if (iscntrl(result) or not isprint(result))
			std::cerr << int(result) << std::endl;
		else
			std::cerr << char(result) << std::endl;
	}

	return result;
}

void sac_parser::retract()
{
	assert(not m_token_value.empty());

	char ch = m_token_value.back();
	if (ch == '\n')
		--m_line_nr;

	m_buffer.push_back(ch == 0 ? std::char_traits<char>::eof() : std::char_traits<char>::to_int_type(ch));
	m_token_value.pop_back();
}

int sac_parser::restart(int start)
{
	int result = 0;

	while (not m_token_value.empty())
		retract();

	switch (start)
	{
		case State::Start:
			result = State::Float;
			break;

		case State::Float:
			result = State::Int;
			break;

		case State::Int:
			result = State::Value;
			break;

		default:
			error("Invalid state in SacParser");
	}

	m_bol = false;

	return result;
}

sac_parser::CIFToken sac_parser::get_next_token()
{
	const auto kEOF = std::char_traits<char>::eof();

	CIFToken result = CIFToken::Unknown;
	int quoteChar = 0;
	int state = State::Start, start = State::Start;
	m_bol = false;

	m_token_value.clear();
	mTokenType = CIFValue::Unknown;

	while (result == CIFToken::Unknown)
	{
		auto ch = get_next_char();

		switch (state)
		{
			case State::Start:
				if (ch == kEOF)
					result = CIFToken::Eof;
				else if (ch == '\n')
				{
					m_bol = true;
					state = State::White;
				}
				else if (ch == ' ' or ch == '\t')
					state = State::White;
				else if (ch == '#')
					state = State::Comment;
				else if (ch == '_')
					state = State::Tag;
				else if (ch == ';' and m_bol)
					state = State::TextField;
				else if (ch == '\'' or ch == '"')
				{
					quoteChar = ch;
					state = State::QuotedString;
				}
				else
					state = start = restart(start);
				break;

			case State::White:
				if (ch == kEOF)
					result = CIFToken::Eof;
				else if (not isspace(ch))
				{
					state = State::Start;
					retract();
					m_token_value.clear();
				}
				else
					m_bol = (ch == '\n');
				break;
			
			case State::Comment:
				if (ch == '\n')
				{
					state = State::Start;
					m_bol = true;
					m_token_value.clear();
				}
				else if (ch == kEOF)
					result = CIFToken::Eof;
				else if (not is_any_print(ch))
					error("invalid character in comment");
				break;

			case State::TextField:
				if (ch == '\n')
					state = State::TextField + 1;
				else if (ch == kEOF)
					error("unterminated textfield");
				// else if (ch == '\\')
				// 	state = State::Esc;
				else if (not is_any_print(ch) and cif::VERBOSE > 2)
					warning("invalid character in text field '" + std::string({static_cast<char>(ch)}) + "' (" + std::to_string((int)ch) + ")");
				break;

			// case State::Esc:
			// 	if (ch == '\n')

			// 	break;

			case State::TextField + 1:
				if (is_text_lead(ch) or ch == ' ' or ch == '\t')
					state = State::TextField;
				else if (ch == ';')
				{
					assert(m_token_value.length() >= 2);
					m_token_value = m_token_value.substr(1, m_token_value.length() - 3);
					mTokenType = CIFValue::TextField;
					result = CIFToken::Value;
				}
				else if (ch == kEOF)
					error("unterminated textfield");
				else if (ch != '\n')
					error("invalid character in text field");
				break;

			case State::QuotedString:
				if (ch == kEOF)
					error("unterminated quoted string");
				else if (ch == quoteChar)
					state = State::QuotedStringQuote;
				else if (not is_any_print(ch) and cif::VERBOSE > 2)
					warning("invalid character in quoted string: '" + std::string({static_cast<char>(ch)}) + "' (" + std::to_string((int)ch) + ")");
				break;

			case State::QuotedStringQuote:
				if (is_white(ch))
				{
					retract();
					result = CIFToken::Value;
					mTokenType = CIFValue::String;

					if (m_token_value.length() < 2)
						error("Invalid quoted string token");

					m_token_value = m_token_value.substr(1, m_token_value.length() - 2);
				}
				else if (ch == quoteChar)
					;
				else if (is_any_print(ch))
					state = State::QuotedString;
				else if (ch == kEOF)
					error("unterminated quoted string");
				else
					error("invalid character in quoted string");
				break;

			case State::Tag:
				if (not is_non_blank(ch))
				{
					retract();
					result = CIFToken::Tag;
				}
				break;

			case State::Float:
				if (ch == '+' or ch == '-')
				{
					state = State::Float + 1;
				}
				else if (isdigit(ch))
					state = State::Float + 1;
				else
					state = start = restart(start);
				break;

			case State::Float + 1:
				//				if (ch == '(')	// numeric???
				//					mState = State::NumericSuffix;
				//				else
				if (ch == '.')
					state = State::Float + 2;
				else if (tolower(ch) == 'e')
					state = State::Float + 3;
				else if (is_white(ch) or ch == kEOF)
				{
					retract();
					result = CIFToken::Value;
					mTokenType = CIFValue::Int;
				}
				else
					state = start = restart(start);
				break;

			// parsed '.'
			case State::Float + 2:
				if (tolower(ch) == 'e')
					state = State::Float + 3;
				else if (is_white(ch) or ch == kEOF)
				{
					retract();
					result = CIFToken::Value;
					mTokenType = CIFValue::Float;
				}
				else
					state = start = restart(start);
				break;

			// parsed 'e'
			case State::Float + 3:
				if (ch == '-' or ch == '+')
					state = State::Float + 4;
				else if (isdigit(ch))
					state = State::Float + 5;
				else
					state = start = restart(start);
				break;

			case State::Float + 4:
				if (isdigit(ch))
					state = State::Float + 5;
				else
					state = start = restart(start);
				break;

			case State::Float + 5:
				if (is_white(ch) or ch == kEOF)
				{
					retract();
					result = CIFToken::Value;
					mTokenType = CIFValue::Float;
				}
				else
					state = start = restart(start);
				break;

			case State::Int:
				if (isdigit(ch) or ch == '+' or ch == '-')
					state = State::Int + 1;
				else
					state = start = restart(start);
				break;

			case State::Int + 1:
				if (is_white(ch) or ch == kEOF)
				{
					retract();
					result = CIFToken::Value;
					mTokenType = CIFValue::Int;
				}
				else
					state = start = restart(start);
				break;

			case State::Value:
				if (ch == '_')
				{
					std::string s = to_lower_copy(m_token_value);

					if (s == "data_")
					{
						state = State::DATA;
						continue;
					}
					
					if (s == "save_")
					{
						state = State::SAVE;
						continue;
					}
				}

				if (result == CIFToken::Unknown and not is_non_blank(ch))
				{
					retract();
					result = CIFToken::Value;

					if (m_token_value == ".")
						mTokenType = CIFValue::Inapplicable;
					else if (iequals(m_token_value, "global_"))
						result = CIFToken::GLOBAL;
					else if (iequals(m_token_value, "stop_"))
						result = CIFToken::STOP;
					else if (iequals(m_token_value, "loop_"))
						result = CIFToken::LOOP;
					else if (m_token_value == "?")
					{
						mTokenType = CIFValue::Unknown;
						m_token_value.clear();
					}
				}
				break;

			case State::DATA:
			case State::SAVE:
				if (not is_non_blank(ch))
				{
					retract();

					if (state == State::DATA)
						result = CIFToken::DATA;
					else
						result = CIFToken::SAVE;

					m_token_value.erase(m_token_value.begin(), m_token_value.begin() + 5);
				}
				break;

			default:
				assert(false);
				error("Invalid state in get_next_token");
				break;
		}
	}

	if (VERBOSE >= 5)
	{
		std::cerr << get_token_name(result);
		if (mTokenType != CIFValue::Unknown)
			std::cerr << ' ' << get_value_name(mTokenType);
		if (result != CIFToken::Eof)
			std::cerr << " " << std::quoted(m_token_value);
		std::cerr << std::endl;
	}

	return result;
}

void sac_parser::match(CIFToken token)
{
	if (m_lookahead != token)
		error(std::string("Unexpected token, expected ") + get_token_name(token) + " but found " + get_token_name(m_lookahead));

	m_lookahead = get_next_token();
}

bool sac_parser::parse_single_datablock(const std::string &datablock)
{
	// first locate the start, as fast as we can
	enum
	{
		start,
		comment,
		string,
		string_quote,
		qstring,
		data
	} state = start;

	int quote = 0;
	bool bol = true;
	std::string dblk = "data_" + datablock;
	std::string::size_type si = 0;
	bool found = false;

	for (auto ch = m_source.sbumpc(); not found and ch != std::streambuf::traits_type::eof(); ch = m_source.sbumpc())
	{
		switch (state)
		{
			case start:
				switch (ch)
				{
					case '#': state = comment; break;
					case 'd':
					case 'D':
						state = data;
						si = 1;
						break;
					case '\'':
					case '"':
						state = string;
						quote = ch;
						break;
					case ';':
						if (bol)
							state = qstring;
						break;
				}
				break;

			case comment:
				if (ch == '\n')
					state = start;
				break;

			case string:
				if (ch == quote)
					state = string_quote;
				break;

			case string_quote:
				if (std::isspace(ch))
					state = start;
				else
					state = string;
				break;

			case qstring:
				if (ch == ';' and bol)
					state = start;
				break;

			case data:
				if (isspace(ch) and dblk[si] == 0)
					found = true;
				else if (dblk[si++] != ch)
					state = start;
				break;
		}

		bol = (ch == '\n');
	}

	if (found)
	{
		produce_datablock(datablock);
		m_lookahead = get_next_token();
		parse_datablock();
	}

	return found;
}

sac_parser::datablock_index sac_parser::index_datablocks()
{
	datablock_index index;

	// first locate the start, as fast as we can
	enum
	{
		start,
		comment,
		string,
		string_quote,
		qstring,
		data,
		data_name
	} state = start;

	int quote = 0;
	bool bol = true;
	const char dblk[] = "data_";
	std::string::size_type si = 0;
	std::string datablock;

	for (auto ch = m_source.sbumpc(); ch != std::streambuf::traits_type::eof(); ch = m_source.sbumpc())
	{
		switch (state)
		{
			case start:
				switch (ch)
				{
					case '#': state = comment; break;
					case 'd':
					case 'D':
						state = data;
						si = 1;
						break;
					case '\'':
					case '"':
						state = string;
						quote = ch;
						break;
					case ';':
						if (bol)
							state = qstring;
						break;
				}
				break;

			case comment:
				if (ch == '\n')
					state = start;
				break;

			case string:
				if (ch == quote)
					state = string_quote;
				break;

			case string_quote:
				if (std::isspace(ch))
					state = start;
				else
					state = string;
				break;

			case qstring:
				if (ch == ';' and bol)
					state = start;
				break;

			case data:
				if (dblk[si] == 0 and is_non_blank(ch))
				{
					datablock = {static_cast<char>(ch)};
					state = data_name;
				}
				else if (dblk[si++] != ch)
					state = start;
				break;

			case data_name:
				if (is_non_blank(ch))
					datablock.insert(datablock.end(), char(ch));
				else if (isspace(ch))
				{
					if (not datablock.empty())
						index[datablock] = m_source.pubseekoff(0, std::ios_base::cur, std::ios_base::in);

					state = start;
				}
				else
					state = start;
				break;
		}

		bol = (ch == '\n');
	}

	return index;
}

bool sac_parser::parse_single_datablock(const std::string &datablock, const datablock_index &index)
{
	bool result = false;

	auto i = index.find(datablock);
	if (i != index.end())
	{
		m_source.pubseekpos(i->second, std::ios_base::in);

		produce_datablock(datablock);
		m_lookahead = get_next_token();
		parse_datablock();

		result = true;
	}

	return result;
}

void sac_parser::parse_file()
{
	while (m_lookahead != CIFToken::Eof)
	{
		switch (m_lookahead)
		{
			case CIFToken::GLOBAL:
				parse_global();
				break;

			case CIFToken::DATA:
				produce_datablock(m_token_value);

				match(CIFToken::DATA);
				parse_datablock();
				break;

			default:
				error("This file does not seem to be an mmCIF file");
				break;
		}
	}
}

void sac_parser::parse_global()
{
	match(CIFToken::GLOBAL);
	while (m_lookahead == CIFToken::Tag)
	{
		match(CIFToken::Tag);
		match(CIFToken::Value);
	}
}

void sac_parser::parse_datablock()
{
	static const std::string kUnitializedCategory("<invalid>");
	std::string cat = kUnitializedCategory;	// intial value acts as a guard for empty category names

	while (m_lookahead == CIFToken::LOOP or m_lookahead == CIFToken::Tag or m_lookahead == CIFToken::SAVE)
	{
		switch (m_lookahead)
		{
			case CIFToken::LOOP:
			{
				cat = kUnitializedCategory; // should start a new category

				match(CIFToken::LOOP);

				std::vector<std::string> tags;

				while (m_lookahead == CIFToken::Tag)
				{
					std::string catName, itemName;
					std::tie(catName, itemName) = split_tag_name(m_token_value);

					if (cat == kUnitializedCategory)
					{
						produce_category(catName);
						cat = catName;
					}
					else if (not iequals(cat, catName))
						error("inconsistent categories in loop_");

					tags.push_back(itemName);

					match(CIFToken::Tag);
				}

				while (m_lookahead == CIFToken::Value)
				{
					produce_row();

					for (auto tag : tags)
					{
						produce_item(cat, tag, m_token_value);
						match(CIFToken::Value);
					}
				}

				cat.clear();
				break;
			}

			case CIFToken::Tag:
			{
				std::string catName, itemName;
				std::tie(catName, itemName) = split_tag_name(m_token_value);

				if (not iequals(cat, catName))
				{
					produce_category(catName);
					cat = catName;
					produce_row();
				}

				match(CIFToken::Tag);

				produce_item(cat, itemName, m_token_value);

				match(CIFToken::Value);
				break;
			}

			case CIFToken::SAVE:
				parse_save_frame();
				break;

			default:
				assert(false);
				break;
		}
	}
}

void sac_parser::parse_save_frame()
{
	error("A regular CIF file should not contain a save frame");
}

// --------------------------------------------------------------------

void parser::produce_datablock(const std::string &name)
{
	if (VERBOSE >= 4)
		std::cerr << "producing data_" << name << std::endl;

	const auto &[iter, ignore] = m_file.emplace(name);
	m_datablock = &(*iter);
}

void parser::produce_category(const std::string &name)
{
	if (VERBOSE >= 4)
		std::cerr << "producing category " << name << std::endl;

	const auto &[cat, ignore] = m_datablock->emplace(name);
	m_category = &*cat;
}

void parser::produce_row()
{
	if (VERBOSE >= 4 and m_category != nullptr)
		std::cerr << "producing row for category " << m_category->name() << std::endl;

	if (m_category == nullptr)
		error("inconsistent categories in loop_");

	m_category->emplace({});
	m_row = m_category->back();
	// m_row.lineNr(m_line_nr);
}

void parser::produce_item(const std::string &category, const std::string &item, const std::string &value)
{
	if (VERBOSE >= 4)
		std::cerr << "producing _" << category << '.' << item << " -> " << value << std::endl;

	if (m_category == nullptr or not iequals(category, m_category->name()))
		error("inconsistent categories in loop_");

	m_row[item] = m_token_value;
}

} // namespace cif