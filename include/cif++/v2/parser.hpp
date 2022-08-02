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

#pragma once

#include <map>
#include <stack>

namespace cif::v2
{

// --------------------------------------------------------------------

class parse_error : public std::runtime_error
{
  public:
	parse_error(uint32_t line_nr, const std::string &message)
		: std::runtime_error("parse error at line " + std::to_string(line_nr) + ": " + message)
	{
	}
};

// --------------------------------------------------------------------

class sac_parser
{
  public:
	using DatablockIndex = std::map<std::string, std::size_t>;

	sac_parser(std::istream &is)
		: mData(is)
	{
		m_validate = true;
		m_line_nr = 1;
		m_bol = true;

		// if (init)
			m_lookahead = get_next_token();
	}

	virtual ~sac_parser() = default;

	enum CharTraitsMask : uint8_t
	{
		kOrdinaryMask = 1 << 0,
		kNonBlankMask = 1 << 1,
		kTextLeadMask = 1 << 2,
		kAnyPrintMask = 1 << 3
	};

	static constexpr bool is_white(int ch)
	{
		return std::isspace(ch) or ch == '#';
	}

	static constexpr bool is_ordinary(int ch)
	{
		return ch >= 0x20 and ch <= 0x7f and (kCharTraitsTable[ch - 0x20] & kOrdinaryMask) != 0;
	}

	static constexpr bool is_non_blank(int ch)
	{
		return ch > 0x20 and ch <= 0x7f and (kCharTraitsTable[ch - 0x20] & kNonBlankMask) != 0;
	}

	static constexpr bool is_text_lead(int ch)
	{
		return ch >= 0x20 and ch <= 0x7f and (kCharTraitsTable[ch - 0x20] & kTextLeadMask) != 0;
	}

	static constexpr bool is_any_print(int ch)
	{
		return ch == '\t' or
		       (ch >= 0x20 and ch <= 0x7f and (kCharTraitsTable[ch - 0x20] & kAnyPrintMask) != 0);
	}

	static bool is_unquoted_string(const char *s)
	{
		auto ss = s;

		bool result = is_ordinary(*s++);
		while (result and *s != 0)
		{
			result = is_non_blank(*s);
			++s;
		}

		// but be careful it does not contain e.g. stop_
		if (result)
		{
			static const std::regex reservedRx(R"((^(?:data|save)|.*(?:loop|stop|global))_.+)", std::regex_constants::icase);
			result = not std::regex_match(ss, reservedRx);
		}

		return result;
	}

  protected:
	static constexpr uint8_t kCharTraitsTable[128] = {
		//	0	1	2	3	4	5	6	7	8	9	a	b	c	d	e	f
		14, 15, 14, 14, 14, 15, 15, 14, 15, 15, 15, 15, 15, 15, 15, 15, //	2
		15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 10, 15, 15, 15, 15, //	3
		15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, //	4
		15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 14, 15, 14, 15, 14, //	5
		15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, //	6
		15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 0,  //	7
	};

	enum class CIFToken
	{
		Unknown,

		Eof,

		DATA,
		LOOP,
		GLOBAL,
		SAVE,
		STOP,
		Tag,
		Value
	};

	static constexpr const char *get_token_name(CIFToken token)
	{
		switch (token)
		{
			case CIFToken::Unknown: return "Unknown";
			case CIFToken::Eof: return "Eof";
			case CIFToken::DATA: return "DATA";
			case CIFToken::LOOP: return "LOOP";
			case CIFToken::GLOBAL: return "GLOBAL";
			case CIFToken::SAVE: return "SAVE";
			case CIFToken::STOP: return "STOP";
			case CIFToken::Tag: return "Tag";
			case CIFToken::Value: return "Value";
		}
	}

	enum class CIFValue
	{
		Int,
		Float,
		Numeric,
		String,
		TextField,
		Inapplicable,
		Unknown
	};

	static constexpr const char *get_value_name(CIFValue type)
	{
		switch (type)
		{
			case CIFValue::Int: return "Int";
			case CIFValue::Float: return "Float";
			case CIFValue::Numeric: return "Numeric";
			case CIFValue::String: return "String";
			case CIFValue::TextField: return "TextField";
			case CIFValue::Inapplicable: return "Inapplicable";
			case CIFValue::Unknown: return "Unknown";
		}
	}

	// getNextChar takes a char from the buffer, or if it is empty
	// from the istream. This function also does carriage/linefeed
	// translation.
	int getNextChar()
	{
		int result;

		if (mBuffer.empty())
			result = mData.get();
		else
		{
			result = mBuffer.top();
			mBuffer.pop();
		}

		// very simple CR/LF translation into LF
		if (result == '\r')
		{
			int lookahead = mData.get();
			if (lookahead != '\n')
				mBuffer.push(lookahead);
			result = '\n';
		}

		mTokenValue += static_cast<char>(result);

		if (result == '\n')
			++m_line_nr;

		if (VERBOSE >= 6)
		{
			std::cerr << "getNextChar => ";
			if (iscntrl(result) or not isprint(result))
				std::cerr << int(result) << std::endl;
			else
				std::cerr << char(result) << std::endl;
		}

		return result;
	}

	void retract()
	{
		assert(not mTokenValue.empty());

		char ch = mTokenValue.back();
		if (ch == '\n')
			--m_line_nr;

		mBuffer.push(ch);
		mTokenValue.pop_back();
	}

	int restart(int start)
	{
		int result = 0;

		while (not mTokenValue.empty())
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

	CIFToken get_next_token()
	{
		const auto kEOF = std::char_traits<char>::eof();

		CIFToken result = CIFToken::Unknown;
		int quoteChar = 0;
		int state = State::Start, start = State::Start;
		m_bol = false;

		mTokenValue.clear();
		mTokenType = CIFValue::Unknown;

		while (result == CIFToken::Unknown)
		{
			auto ch = getNextChar();

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
						mTokenValue.clear();
					}
					else
						m_bol = (ch == '\n');
					break;

				case State::Comment:
					if (ch == '\n')
					{
						state = State::Start;
						m_bol = true;
						mTokenValue.clear();
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
					else if (not is_any_print(ch))
						warning("invalid character in text field '" + std::string({static_cast<char>(ch)}) + "' (" + std::to_string((int)ch) + ")");
					break;

				case State::TextField + 1:
					if (is_text_lead(ch) or ch == ' ' or ch == '\t')
						state = State::TextField;
					else if (ch == ';')
					{
						assert(mTokenValue.length() >= 2);
						mTokenValue = mTokenValue.substr(1, mTokenValue.length() - 3);
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
					else if (not is_any_print(ch))
						warning("invalid character in quoted string: '" + std::string({static_cast<char>(ch)}) + '\'');
					break;

				case State::QuotedStringQuote:
					if (is_white(ch))
					{
						retract();
						result = CIFToken::Value;
						mTokenType = CIFValue::String;

						if (mTokenValue.length() < 2)
							error("Invalid quoted string token");

						mTokenValue = mTokenValue.substr(1, mTokenValue.length() - 2);
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
						std::string s = toLowerCopy(mTokenValue);

						if (s == "global_")
							result = CIFToken::GLOBAL;
						else if (s == "stop_")
							result = CIFToken::STOP;
						else if (s == "loop_")
							result = CIFToken::LOOP;
						else if (s == "data_")
						{
							state = State::DATA;
							continue;
						}
						else if (s == "save_")
						{
							state = State::SAVE;
							continue;
						}
					}

					if (result == CIFToken::Unknown and not is_non_blank(ch))
					{
						retract();
						result = CIFToken::Value;

						if (mTokenValue == ".")
							mTokenType = CIFValue::Inapplicable;
						else if (mTokenValue == "?")
						{
							mTokenType = CIFValue::Unknown;
							mTokenValue.clear();
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

						mTokenValue.erase(mTokenValue.begin(), mTokenValue.begin() + 5);
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
				std::cerr << " " << std::quoted(mTokenValue);
			std::cerr << std::endl;
		}

		return result;
	}

	void match(CIFToken token)
	{
		if (m_lookahead != token)
			error(std::string("Unexpected token, expected ") + get_token_name(token) + " but found " + get_token_name(m_lookahead));

		m_lookahead = get_next_token();
	}

  public:
	bool parseSingleDatablock(const std::string &datablock)
	{
		// first locate the start, as fast as we can
		auto &sb = *mData.rdbuf();

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

		for (auto ch = sb.sbumpc(); not found and ch != std::streambuf::traits_type::eof(); ch = sb.sbumpc())
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
			produceDatablock(datablock);
			m_lookahead = get_next_token();
			parseDataBlock();
		}

		return found;
	}

	DatablockIndex indexDatablocks()
	{
		DatablockIndex index;

		// first locate the start, as fast as we can
		auto &sb = *mData.rdbuf();

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

		for (auto ch = sb.sbumpc(); ch != std::streambuf::traits_type::eof(); ch = sb.sbumpc())
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
							index[datablock] = mData.tellg();

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

	bool parseSingleDatablock(const std::string &datablock, const DatablockIndex &index)
	{
		bool result = false;

		auto i = index.find(datablock);
		if (i != index.end())
		{
			mData.seekg(i->second);

			produceDatablock(datablock);
			m_lookahead = get_next_token();
			parseDataBlock();

			result = true;
		}

		return result;
	}

	void parseFile()
	{
		while (m_lookahead != CIFToken::Eof)
		{
			switch (m_lookahead)
			{
				case CIFToken::GLOBAL:
					parseGlobal();
					break;

				case CIFToken::DATA:
					produceDatablock(mTokenValue);

					match(CIFToken::DATA);
					parseDataBlock();
					break;

				default:
					error("This file does not seem to be an mmCIF file");
					break;
			}
		}
	}

  protected:
	void parseGlobal()
	{
		match(CIFToken::GLOBAL);
		while (m_lookahead == CIFToken::Tag)
		{
			match(CIFToken::Tag);
			match(CIFToken::Value);
		}
	}

	void parseDataBlock()
	{
		std::string cat;

		while (m_lookahead == CIFToken::LOOP or m_lookahead == CIFToken::Tag or m_lookahead == CIFToken::SAVE)
		{
			switch (m_lookahead)
			{
				case CIFToken::LOOP:
				{
					cat.clear(); // should start a new category

					match(CIFToken::LOOP);

					std::vector<std::string> tags;

					while (m_lookahead == CIFToken::Tag)
					{
						std::string catName, itemName;
						std::tie(catName, itemName) = splitTagName(mTokenValue);

						if (cat.empty())
						{
							produceCategory(catName);
							cat = catName;
						}
						else if (not iequals(cat, catName))
							error("inconsistent categories in loop_");

						tags.push_back(itemName);

						match(CIFToken::Tag);
					}

					while (m_lookahead == CIFToken::Value)
					{
						produceRow();

						for (auto tag : tags)
						{
							produceItem(cat, tag, mTokenValue);
							match(CIFToken::Value);
						}
					}

					cat.clear();
					break;
				}

				case CIFToken::Tag:
				{
					std::string catName, itemName;
					std::tie(catName, itemName) = splitTagName(mTokenValue);

					if (not iequals(cat, catName))
					{
						produceCategory(catName);
						cat = catName;
						produceRow();
					}

					match(CIFToken::Tag);

					produceItem(cat, itemName, mTokenValue);

					match(CIFToken::Value);
					break;
				}

				case CIFToken::SAVE:
					parseSaveFrame();
					break;

				default:
					assert(false);
					break;
			}
		}
	}

	virtual void parseSaveFrame()
	{
		error("A regular CIF file should not contain a save frame");
	}

	void error(const std::string &msg)
	{
		throw parse_error(m_line_nr, msg);
	}

	void warning(const std::string &msg)
	{
		std::cerr << "parser warning at line" << m_line_nr << ": " << msg << std::endl;
	}

	// production methods, these are pure virtual here

	virtual void produceDatablock(const std::string &name) = 0;
	virtual void produceCategory(const std::string &name) = 0;
	virtual void produceRow() = 0;
	virtual void produceItem(const std::string &category, const std::string &item, const std::string &value) = 0;

  protected:
	enum State
	{
		Start,
		White,
		Comment,
		QuestionMark,
		Dot,
		QuotedString,
		QuotedStringQuote,
		UnquotedString,
		Tag,
		TextField,
		Float = 100,
		Int = 110,
		Value = 300,
		DATA,
		SAVE
	};

	std::istream &mData;

	// Parser state
	bool m_validate;
	uint32_t m_line_nr;
	bool m_bol;
	CIFToken m_lookahead;
	std::string mTokenValue;
	CIFValue mTokenType;
	std::stack<int> mBuffer;
};

// --------------------------------------------------------------------

template <
	typename File,
	typename Datablock,
	typename Category>
class parser_t : public sac_parser
{
  public:
	using file_type = File;
	using datablock_type = Datablock;
	using category_type = Category;
	using row_handle_type = category_type::reference;

	parser_t(std::istream &is, file_type &file)
		: sac_parser(is)
		, m_file(file)
	{
	}

	void produceDatablock(const std::string &name) override
	{
		std::tie(m_datablock, std::ignore) = m_file.emplace(name);
	}

	void produceCategory(const std::string &name) override
	{
		if (VERBOSE >= 4)
			std::cerr << "producing category " << name << std::endl;

		std::tie(m_category, std::ignore) = m_datablock->emplace(name);
	}

	void produceRow() override
	{
		if (VERBOSE >= 4)
			std::cerr << "producing row for category " << m_category->name() << std::endl;

		m_category->emplace({});
		m_row = m_category->back();
		// m_row.lineNr(m_line_nr);
	}

	void produceItem(const std::string &category, const std::string &item, const std::string &value) override
	{
		if (VERBOSE >= 4)
			std::cerr << "producing _" << category << '.' << item << " -> " << value << std::endl;

		if (not iequals(category, m_category->name()))
			error("inconsistent categories in loop_");

		m_row[item] = mTokenValue;
	}

  protected:
	file_type &m_file;
	file_type::iterator m_datablock;
	datablock_type::iterator m_category;
	row_handle_type m_row;
};

// class Parser : public SacParser
// {
//   public:
// 	Parser(std::istream &is, File &f, bool init = true);

// 	virtual void produceDatablock(const std::string &name);
// 	virtual void produceCategory(const std::string &name);
// 	virtual void produceRow();
// 	virtual void produceItem(const std::string &category, const std::string &item, const std::string &value);

//   protected:
// 	File &mFile;
// 	Datablock *mDataBlock;
// 	Datablock::iterator m_category;
// 	Row mRow;
// };

// // --------------------------------------------------------------------

// class DictParser : public Parser
// {
//   public:
// 	DictParser(Validator &validator, std::istream &is);
// 	~DictParser();

// 	void loadDictionary();

//   private:
// 	virtual void parseSaveFrame();

// 	bool collectItemTypes();
// 	void linkItems();

// 	Validator &mValidator;
// 	File mFile;
// 	struct DictParserDataImpl *mImpl;
// 	bool mCollectedItemTypes = false;
// };

} // namespace cif::v2