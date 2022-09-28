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

#include <atomic>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <thread>

#if defined(_MSC_VER)
#define TERM_WIDTH 80
#else
#include <sys/ioctl.h>
#include <termios.h>
#endif

#include <cif++/utilities.hpp>

#include "revision.hpp"

namespace fs = std::filesystem;

// --------------------------------------------------------------------

namespace cif
{

int VERBOSE = 0;

// --------------------------------------------------------------------

std::string get_version_nr()
{
	std::ostringstream s;
	write_version_string(s, false);
	return s.str();
}

// --------------------------------------------------------------------

#ifdef _MSC_VER
}
#include <Windows.h>
#include <libloaderapi.h>
#include <wincon.h>

#include <codecvt>

namespace cif
{

uint32_t get_terminal_width()
{
	return TERM_WIDTH;
}

std::string GetExecutablePath()
{
	WCHAR buffer[4096];

	DWORD n = ::GetModuleFileNameW(nullptr, buffer, sizeof(buffer) / sizeof(WCHAR));
	if (n == 0)
		throw std::runtime_error("could not get exe path");

	std::wstring ws(buffer);

	// convert from utf16 to utf8
	std::wstring_convert<std::codecvt_utf8<wchar_t>> conv1;
	std::string u8str = conv1.to_bytes(ws);

	return u8str;
}

#else

#include <limits.h>

uint32_t get_terminal_width()
{
	uint32_t result = 80;

	if (isatty(STDOUT_FILENO))
	{
		struct winsize w;
		ioctl(0, TIOCGWINSZ, &w);
		result = w.ws_col;
	}
	return result;
}

std::string get_executable_path()
{
	using namespace std::literals;

	char path[PATH_MAX] = "";
	if (readlink("/proc/self/exe", path, sizeof(path)) == -1)
		throw std::runtime_error("could not get exe path "s + strerror(errno));
	return {path};
}

#endif

// --------------------------------------------------------------------

struct ProgressImpl
{
	ProgressImpl(int64_t inMax, const std::string &inAction)
		: mMax(inMax)
		, mConsumed(0)
		, mAction(inAction)
		, mMessage(inAction)
		, mThread(std::bind(&ProgressImpl::Run, this))
	{
	}

	void Run();
	void Stop()
	{
		mStop = true;
		if (mThread.joinable())
			mThread.join();
	}

	void PrintProgress();
	void PrintDone();

	int64_t mMax;
	std::atomic<int64_t> mConsumed;
	int64_t mLastConsumed = 0;
	int mSpinnerIndex = 0;
	std::string mAction, mMessage;
	std::mutex mMutex;
	std::thread mThread;
	std::chrono::time_point<std::chrono::system_clock>
		mStart = std::chrono::system_clock::now();
	bool mStop = false;
};

void ProgressImpl::Run()
{
	bool printedAny = false;

	try
	{
		for (;;)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));

			std::unique_lock lock(mMutex);

			if (mStop or mConsumed == mMax)
				break;

			auto elapsed = std::chrono::system_clock::now() - mStart;

			if (elapsed < std::chrono::seconds(5))
				continue;

			PrintProgress();
			printedAny = true;
		}
	}
	catch (...)
	{
	}

	if (printedAny)
		PrintDone();
}

void ProgressImpl::PrintProgress()
{
	//	const char* kBlocks[] = {
	//		" ",				// 0
	//		u8"\u258F",			// 1
	//		u8"\u258E",			// 2
	//		u8"\u258D",			// 3
	//		u8"\u258C",			// 4
	//		u8"\u258B",			// 5
	//		u8"\u258A",			// 6
	//		u8"\u2589",			// 7
	//		u8"\u2588",			// 8
	//	};

	const char *kBlocks[] = {
		" ", // 0
		" ", // 1
		" ", // 2
		"-", // 3
		"-", // 4
		"-", // 5
		"=", // 6
		"=", // 7
		"=", // 8
	};

	uint32_t width = get_terminal_width();

	std::string msg;
	msg.reserve(width + 1);
	if (mMessage.length() <= 20)
	{
		msg = mMessage;
		if (msg.length() < 20)
			msg.append(20 - msg.length(), ' ');
	}
	else
		msg = mMessage.substr(0, 17) + "...";

	msg += " |";

	int64_t consumed = mConsumed;
	float progress = static_cast<float>(consumed) / mMax;
	int pi = static_cast<int>(std::ceil(progress * 33 * 8));
	//	int tw = width - 28;
	//	int twd = static_cast<int>(tw * progress + 0.5f);
	//	msg.append(twd, '=');
	//	msg.append(tw - twd, ' ');

	for (int i = 0; i < 33; ++i)
	{
		if (pi <= 0)
			msg += kBlocks[0];
		else if (pi >= 8)
			msg += kBlocks[8];
		else
			msg += kBlocks[pi];
		pi -= 8;
	}

	msg.append("| ");

	const char kSpinner[] = {' ', '.', 'o', 'O', '0', 'O', 'o', '.'};
	const size_t kSpinnerCount = sizeof(kSpinner);

	if (mLastConsumed < consumed)
	{
		mLastConsumed = consumed;
		mSpinnerIndex = (mSpinnerIndex + 1) % kSpinnerCount;
	}

	const char spinner[2] = {kSpinner[mSpinnerIndex], 0};
	msg.append(spinner);

	//	int perc = static_cast<int>(100 * progress);
	//	if (perc < 100)
	//		msg += ' ';
	//	if (perc < 10)
	//		msg += ' ';
	//	msg += to_string(perc);
	//	msg += '%';

	std::cout << '\r' << msg;
	std::cout.flush();
}

namespace
{

	std::ostream &operator<<(std::ostream &os, const std::chrono::duration<double> &t)
	{
		uint64_t s = static_cast<uint64_t>(std::trunc(t.count()));
		if (s > 24 * 60 * 60)
		{
			auto days = s / (24 * 60 * 60);
			os << days << "d ";
			s %= 24 * 60 * 60;
		}

		if (s > 60 * 60)
		{
			auto hours = s / (60 * 60);
			os << hours << "h ";
			s %= 60 * 60;
		}

		if (s > 60)
		{
			auto minutes = s / 60;
			os << minutes << "m ";
			s %= 60;
		}

		double ss = s + 1e-6 * (t.count() - s);

		os << std::fixed << std::setprecision(1) << ss << 's';

		return os;
	}

} // namespace

void ProgressImpl::PrintDone()
{
	std::chrono::duration<double> elapsed = std::chrono::system_clock::now() - mStart;

	std::ostringstream msgstr;
	msgstr << mAction << " done in " << elapsed << " seconds";
	auto msg = msgstr.str();

	uint32_t width = get_terminal_width();

	if (msg.length() < width)
		msg += std::string(width - msg.length(), ' ');

	std::cout << '\r' << msg << std::endl;
}

Progress::Progress(int64_t inMax, const std::string &inAction)
	: m_impl(nullptr)
{
	if (isatty(STDOUT_FILENO))
		m_impl = new ProgressImpl(inMax, inAction);
}

Progress::~Progress()
{
	if (m_impl != nullptr)
		m_impl->Stop();

	delete m_impl;
}

void Progress::consumed(int64_t inConsumed)
{
	if (m_impl != nullptr and
		(m_impl->mConsumed += inConsumed) >= m_impl->mMax)
	{
		m_impl->Stop();
	}
}

void Progress::progress(int64_t inProgress)
{
	if (m_impl != nullptr and
		(m_impl->mConsumed = inProgress) >= m_impl->mMax)
	{
		m_impl->Stop();
	}
}

void Progress::message(const std::string &inMessage)
{
	if (m_impl != nullptr)
	{
		std::unique_lock lock(m_impl->mMutex);
		m_impl->mMessage = inMessage;
	}
}

} // namespace cif

// --------------------------------------------------------------------
//
// Try to find a named resource. Can be either a local file with this name,
// a file located in our cache directory or a resource linked in with mrc.
//
// We have a special, private version of mrsrc here. To be able to create
// shared libraries and still be able to link when there's no mrc used.

namespace mrsrc
{
/// \brief Internal data structure as generated by mrc
struct rsrc_imp
{
	unsigned int m_next;
	unsigned int m_child;
	unsigned int m_name;
	unsigned int m_size;
	unsigned int m_data;
};
} // namespace mrsrc

#if _MSC_VER

extern "C" const mrsrc::rsrc_imp *gResourceIndexDefault[1] = {};
extern "C" const char *gResourceDataDefault[1] = {};
extern "C" const char *gResourceNameDefault[1] = {};

extern "C" const mrsrc::rsrc_imp gResourceIndex[];
extern "C" const char gResourceData[];
extern "C" const char gResourceName[];

#pragma comment(linker, "/alternatename:gResourceIndex=gResourceIndexDefault")
#pragma comment(linker, "/alternatename:gResourceData=gResourceDataDefault")
#pragma comment(linker, "/alternatename:gResourceName=gResourceNameDefault")

#else
extern const __attribute__((weak)) mrsrc::rsrc_imp gResourceIndex[];
extern const __attribute__((weak)) char gResourceData[];
extern const __attribute__((weak)) char gResourceName[];

const mrsrc::rsrc_imp gResourceIndex[1] = {};
const char gResourceData[1] = {};
const char gResourceName[1] = {};

#endif

namespace mrsrc
{
class rsrc_data
{
  public:
	static rsrc_data &instance()
	{
		static rsrc_data s_instance;
		return s_instance;
	}

	const rsrc_imp *index() const { return m_index; }

	const char *data(unsigned int offset) const
	{
		return m_data + offset;
	}

	const char *name(unsigned int offset) const
	{
		return m_name + offset;
	}

  private:
	rsrc_data()
	{
		// if (gResourceIndex and (gResourceIndex[0].m_child > 0 or gResourceIndex[0].m_size > 0) and gResourceIndex and gResourceName)
		if (gResourceIndex[0].m_child > 0 or gResourceIndex[0].m_size > 0)
		{
			m_index = gResourceIndex;
			m_data = gResourceData;
			m_name = gResourceName;
		}
	}

	rsrc_imp m_dummy = {};
	const rsrc_imp *m_index = &m_dummy;
	const char *m_data = "";
	const char *m_name = "";
};

/// \brief Class mrsrc::rsrc contains a pointer to the data in the
/// resource, as well as offering an iterator interface to its
/// children.

class rsrc
{
  public:
	rsrc()
		: m_impl(rsrc_data::instance().index())
	{
	}

	rsrc(const rsrc &other)
		: m_impl(other.m_impl)
	{
	}

	rsrc &operator=(const rsrc &other)
	{
		m_impl = other.m_impl;
		return *this;
	}

	rsrc(std::filesystem::path path);

	std::string name() const { return rsrc_data::instance().name(m_impl->m_name); }

	const char *data() const { return rsrc_data::instance().data(m_impl->m_data); }

	unsigned long size() const { return m_impl->m_size; }

	explicit operator bool() const { return m_impl != NULL and m_impl->m_size > 0; }

	template <typename RSRC>
	class iterator_t
	{
	  public:
		using iterator_category = std::input_iterator_tag;
		using value_type = RSRC;
		using difference_type = std::ptrdiff_t;
		using pointer = value_type *;
		using reference = value_type &;

		iterator_t(const rsrc_imp *cur)
			: m_cur(cur)
		{
		}

		iterator_t(const iterator_t &i)
			: m_cur(i.m_cur)
		{
		}

		iterator_t &operator=(const iterator_t &i)
		{
			m_cur = i.m_cur;
			return *this;
		}

		reference operator*() { return m_cur; }
		pointer operator->() { return &m_cur; }

		iterator_t &operator++()
		{
			if (m_cur.m_impl->m_next)
				m_cur.m_impl = rsrc_data::instance().index() + m_cur.m_impl->m_next;
			else
				m_cur.m_impl = nullptr;
			return *this;
		}

		iterator_t operator++(int)
		{
			auto tmp(*this);
			this->operator++();
			return tmp;
		}

		bool operator==(const iterator_t &rhs) const { return m_cur.m_impl == rhs.m_cur.m_impl; }
		bool operator!=(const iterator_t &rhs) const { return m_cur.m_impl != rhs.m_cur.m_impl; }

	  private:
		value_type m_cur;
	};

	using iterator = iterator_t<rsrc>;

	iterator begin() const
	{
		const rsrc_imp *impl = nullptr;
		if (m_impl and m_impl->m_child)
			impl = rsrc_data::instance().index() + m_impl->m_child;
		return iterator(impl);
	}

	iterator end() const
	{
		return iterator(nullptr);
	}

  private:
	rsrc(const rsrc_imp *imp)
		: m_impl(imp)
	{
	}

	const rsrc_imp *m_impl;
};

inline rsrc::rsrc(std::filesystem::path p)
{
	m_impl = rsrc_data::instance().index();

	// using std::filesytem::path would have been natural here of course...

	auto pb = p.begin();
	auto pe = p.end();

	while (m_impl != nullptr and pb != pe)
	{
		auto name = *pb++;

		const rsrc_imp *impl = nullptr;
		for (rsrc child : *this)
		{
			if (child.name() == name)
			{
				impl = child.m_impl;
				break;
			}
		}

		m_impl = impl;
	}

	if (pb != pe) // not found
		m_impl = nullptr;
}

// --------------------------------------------------------------------

template <typename CharT, typename Traits>
class basic_streambuf : public std::basic_streambuf<CharT, Traits>
{
  public:
	typedef CharT char_type;
	typedef Traits traits_type;
	typedef typename traits_type::int_type int_type;
	typedef typename traits_type::pos_type pos_type;
	typedef typename traits_type::off_type off_type;

	/// \brief constructor taking a \a path to the resource in memory
	basic_streambuf(const std::string &path)
		: m_rsrc(path)
	{
		init();
	}

	/// \brief constructor taking a \a rsrc
	basic_streambuf(const rsrc &rsrc)
		: m_rsrc(rsrc)
	{
		init();
	}

	basic_streambuf(const basic_streambuf &) = delete;

	basic_streambuf(basic_streambuf &&rhs)
		: basic_streambuf(rhs.m_rsrc)
	{
	}

	basic_streambuf &operator=(const basic_streambuf &) = delete;

	basic_streambuf &operator=(basic_streambuf &&rhs)
	{
		swap(rhs);
		return *this;
	}

	void swap(basic_streambuf &rhs)
	{
		std::swap(m_begin, rhs.m_begin);
		std::swap(m_end, rhs.m_end);
		std::swap(m_current, rhs.m_current);
	}

  private:
	void init()
	{
		m_begin = reinterpret_cast<const char_type *>(m_rsrc.data());
		m_end = reinterpret_cast<const char_type *>(m_rsrc.data() + m_rsrc.size());
		m_current = m_begin;
	}

	int_type underflow()
	{
		if (m_current == m_end)
			return traits_type::eof();

		return traits_type::to_int_type(*m_current);
	}

	int_type uflow()
	{
		if (m_current == m_end)
			return traits_type::eof();

		return traits_type::to_int_type(*m_current++);
	}

	int_type pbackfail(int_type ch)
	{
		if (m_current == m_begin or (ch != traits_type::eof() and ch != m_current[-1]))
			return traits_type::eof();

		return traits_type::to_int_type(*--m_current);
	}

	std::streamsize showmanyc()
	{
		assert(std::less_equal<const char *>()(m_current, m_end));
		return m_end - m_current;
	}

	pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which)
	{
		switch (dir)
		{
			case std::ios_base::beg:
				m_current = m_begin + off;
				break;

			case std::ios_base::end:
				m_current = m_end + off;
				break;

			case std::ios_base::cur:
				m_current += off;
				break;

			default:
				break;
		}

		if (m_current < m_begin)
			m_current = m_begin;

		if (m_current > m_end)
			m_current = m_end;

		return m_current - m_begin;
	}

	pos_type seekpos(pos_type pos, std::ios_base::openmode which)
	{
		m_current = m_begin + pos;

		if (m_current < m_begin)
			m_current = m_begin;

		if (m_current > m_end)
			m_current = m_end;

		return m_current - m_begin;
	}

  private:
	rsrc m_rsrc;
	const char_type *m_begin;
	const char_type *m_end;
	const char_type *m_current;
};

using streambuf = basic_streambuf<char, std::char_traits<char>>;

// --------------------------------------------------------------------
// class mrsrc::istream

template <typename CharT, typename Traits>
class basic_istream : public std::basic_istream<CharT, Traits>
{
  public:
	typedef CharT char_type;
	typedef Traits traits_type;
	typedef typename traits_type::int_type int_type;
	typedef typename traits_type::pos_type pos_type;
	typedef typename traits_type::off_type off_type;

  private:
	using __streambuf_type = basic_streambuf<CharT, Traits>;
	using __istream_type = std::basic_istream<CharT, Traits>;

	__streambuf_type m_buffer;

  public:
	basic_istream(const std::string &path)
		: __istream_type(&m_buffer)
		, m_buffer(path)
	{
		this->init(&m_buffer);
	}

	basic_istream(rsrc &resource)
		: __istream_type(&m_buffer)
		, m_buffer(resource)
	{
		this->init(&m_buffer);
	}

	basic_istream(const basic_istream &) = delete;

	basic_istream(basic_istream &&rhs)
		: __istream_type(std::move(rhs))
		, m_buffer(std::move(rhs.m_buffer))
	{
		__istream_type::set_rdbuf(&m_buffer);
	}

	basic_istream &operator=(const basic_istream &) = delete;

	basic_istream &operator=(basic_istream &&rhs)
	{
		__istream_type::operator=(std::move(rhs));
		m_buffer = std::move(rhs.m_buffer);
		return *this;
	}

	void swap(basic_istream &rhs)
	{
		__istream_type::swap(rhs);
		m_buffer.swap(rhs.m_buffer);
	}

	__streambuf_type *rdbuf() const
	{
		return const_cast<__streambuf_type *>(&m_buffer);
	}
};

using istream = basic_istream<char, std::char_traits<char>>;
} // namespace mrsrc

// --------------------------------------------------------------------

namespace cif
{

// --------------------------------------------------------------------

class ResourcePool
{
  public:
	static ResourcePool &instance()
	{
		static std::unique_ptr<ResourcePool> s_instance(new ResourcePool);
		return *s_instance;
	}

	void pushDir(fs::path dir)
	{
		std::error_code ec;

		if (fs::exists(dir, ec) and not ec)
			mDirs.push_front(dir);
	}

	void pushDir(const char *path)
	{
		if (path != nullptr)
			pushDir(fs::path(path));
	}

	void pushAlias(const std::string &name, std::filesystem::path dataFile)
	{
		std::error_code ec;
		if (not fs::exists(dataFile, ec) or ec)
			throw std::runtime_error("Attempt to add a file resource for " + name + " that cannot be used (" + dataFile.string() + ") :" + ec.message());

		mLocalResources[name] = dataFile;
	}

	std::unique_ptr<std::istream> load(fs::path name);

  private:
	ResourcePool();

	std::unique_ptr<std::ifstream> open(fs::path &p)
	{
		std::unique_ptr<std::ifstream> result;

		try
		{
			if (fs::exists(p))
			{
				std::unique_ptr<std::ifstream> file(new std::ifstream(p, std::ios::binary));
				if (file->is_open())
					result.reset(file.release());
			}
		}
		catch (...)
		{
		}

		return result;
	}

	std::map<std::string, std::filesystem::path> mLocalResources;
	std::deque<fs::path> mDirs;
};

ResourcePool::ResourcePool()
{
#if defined(DATA_DIR)
	pushDir(DATA_DIR);
#endif

	pushDir(getenv("LIBCIFPP_DATA_DIR"));

	auto ccp4 = getenv("CCP4");
	if (ccp4 != nullptr)
		pushDir(fs::path(ccp4) / "share" / "libcifpp");

#if defined(CACHE_DIR)
	pushDir(CACHE_DIR);
#endif
}

std::unique_ptr<std::istream> ResourcePool::load(fs::path name)
{
	std::unique_ptr<std::istream> result;
	std::error_code ec;

	fs::path p = name;

	if (mLocalResources.count(name.string()))
		result = open(mLocalResources[name.string()]);

	for (auto di = mDirs.begin(); not result and di != mDirs.end(); ++di)
	{
		auto p2 = *di / p;
		if (fs::exists(p2, ec) and not ec)
			result = open(p2);
	}

	// if (not result and gResourceData)
	if (not result and (gResourceIndex[0].m_child > 0 or gResourceIndex[0].m_size > 0))
	{
		mrsrc::rsrc rsrc(name);
		if (rsrc)
			result.reset(new mrsrc::istream(rsrc));
	}

	return result;
}

// --------------------------------------------------------------------

void add_data_directory(std::filesystem::path dataDir)
{
	ResourcePool::instance().pushDir(dataDir);
}

void add_file_resource(const std::string &name, std::filesystem::path dataFile)
{
	ResourcePool::instance().pushAlias(name, dataFile);
}

std::unique_ptr<std::istream> load_resource(std::filesystem::path name)
{
	return ResourcePool::instance().load(name);
}

} // namespace cif
