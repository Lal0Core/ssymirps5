// SSYmirPS5 frontend: which game a Saturn disc is, and where its back cover is online (fe_backsources.h).
// SPDX-License-Identifier: MIT

#include "fe_backsources.h"

#include "fe_artmatch.h"
#include "fe_coverfetch.h"
#include "third_party/unzip.h"

#include "OrbisPaths.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <sys/stat.h>

#define YMIR_INCBIN(sym, path)                                                                                \
	__asm__(".section .rodata\n"                                                                               \
			".balign 16\n"                                                                                     \
			".global " #sym "_begin\n" #sym "_begin:\n"                                                        \
			".incbin \"" path "\"\n"                                                                           \
			".global " #sym "_end\n" #sym "_end:\n"                                                            \
			".previous\n");                                                                                    \
	extern "C" const char sym##_begin[];                                                                       \
	extern "C" const char sym##_end[];

YMIR_INCBIN(ymir_saturn_serials, SATURN_SERIALS_TSV)
YMIR_INCBIN(ymir_chromagi, CHROMAGI_TSV)

namespace fe
{
namespace backs
{
namespace
{
// "MK-81014" / "mk81014" / "MK 81014" -> "MK81014"
std::string Norm(const std::string& code)
{
	std::string out;
	for (char c : code)
	{
		if (c >= 'a' && c <= 'z')
			out += char(c - 'a' + 'A');
		else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
			out += c;
	}
	return out;
}

std::vector<std::string> Split(const std::string& s, char sep)
{
	std::vector<std::string> out;
	size_t a = 0;
	for (;;)
	{
		const size_t b = s.find(sep, a);
		out.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
		if (b == std::string::npos)
			return out;
		a = b + 1;
	}
}

template <typename Fn>
void Lines(const char* p, const char* end, Fn fn)
{
	while (p < end)
	{
		const char* nl = static_cast<const char*>(memchr(p, '\n', size_t(end - p)));
		const char* e = nl ? nl : end;
		std::string line(p, size_t(e - p));
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (!line.empty() && line[0] != '#')
			fn(line);
		p = e + 1;
	}
}

struct Serial
{
	std::string title, region;
};
const std::unordered_map<std::string, Serial>& Serials()
{
	static std::unordered_map<std::string, Serial> table;
	static std::once_flag once;
	std::call_once(once, [] {
		Lines(ymir_saturn_serials_begin, ymir_saturn_serials_end, [](const std::string& line) {
			const std::vector<std::string> f = Split(line, '\t');
			if (f.size() >= 2 && !f[0].empty())
				table.emplace(Norm(f[0]), Serial{f[1], f.size() > 2 ? f[2] : std::string()});
		});
	});
	return table;
}

// The region a name or table says, as fe_artmatch.h writes it.
std::string RegionWord(const std::string& r)
{
	const std::string l = artmatch::Lower(r);
	if (l == "japan" || l == "ntsc-j" || l == "jp")
		return "Japan";
	if (l == "usa" || l == "north america" || l == "united states" || l == "canada" || l == "ntsc-u" || l == "us")
		return "USA";
	if (l == "europe" || l == "pal" || l == "united kingdom" || l == "germany" || l == "france" || l == "spain" ||
		l == "italy" || l == "australia" || l == "the netherlands" || l == "netherlands" || l == "sweden" || l == "eu")
		return "Europe";
	if (l == "world")
		return "World";
	return "";
}

struct ChromagiRow
{
	std::string slug, title, region;
	std::vector<std::string> codes; // normalized
};
const std::vector<ChromagiRow>& Chromagi()
{
	static std::vector<ChromagiRow> rows;
	static std::once_flag once;
	std::call_once(once, [] {
		Lines(ymir_chromagi_begin, ymir_chromagi_end, [](const std::string& line) {
			const std::vector<std::string> f = Split(line, '\t');
			if (f.size() < 3)
				return;
			ChromagiRow r;
			r.slug = f[0];
			r.title = f[2];
			for (const std::string& c : Split(f[1], ','))
				if (!Norm(c).empty())
					r.codes.push_back(Norm(c));
			// a European release's number ends in 50 ("MK81014-50", "T-8113H-50"); "GS-" / "T-...G" are Japanese
			for (const std::string& c : Split(f[1], ','))
			{
				const std::string u = Norm(c);
				if (r.region.empty() && u.size() > 2 && u.compare(u.size() - 2, 2, "50") == 0 && c.find('-') != std::string::npos)
					r.region = "Europe";
			}
			if (r.region.empty() && (r.slug.rfind("gs-", 0) == 0 || (r.slug.rfind("t-", 0) == 0 && r.slug.back() == 'g')))
				r.region = "Japan";
			if (r.region.empty() && (r.slug.rfind("f", 0) == 0 || r.slug.rfind("vf", 0) == 0))
				r.region = "Europe"; // the box codes (FVIR16SSC) are European
			rows.push_back(r);
		});
	});
	return rows;
}

std::string WithRegion(const std::string& name, const std::string& region)
{
	return region.empty() ? name : name + " (" + region + ")";
}

// The best match of `names` (each with its region) among `list`: the index into list, or -1.
int Match(const std::vector<std::string>& list, const std::vector<std::string>& queries, const std::string& region)
{
	if (list.empty())
		return -1;
	artmatch::Index ix;
	artmatch::Build(ix, list);
	for (const std::string& q : queries)
	{
		if (q.empty())
			continue;
		const std::string hit = artmatch::Resolve(ix, q, region, false);
		if (hit.empty())
			continue;
		for (size_t i = 0; i < list.size(); i++)
			if (list[i] == hit)
				return int(i);
	}
	return -1;
}

std::string XmlText(const std::string& s)
{
	if (s.find('&') == std::string::npos)
		return s;
	std::string out;
	for (size_t i = 0; i < s.size(); i++)
	{
		if (s[i] != '&')
		{
			out += s[i];
			continue;
		}
		const size_t semi = s.find(';', i);
		if (semi == std::string::npos || semi - i > 8)
		{
			out += s[i];
			continue;
		}
		const std::string ent = s.substr(i + 1, semi - i - 1);
		if (ent == "amp")
			out += '&';
		else if (ent == "lt")
			out += '<';
		else if (ent == "gt")
			out += '>';
		else if (ent == "quot")
			out += '"';
		else if (ent == "apos")
			out += '\'';
		else if (!ent.empty() && ent[0] == '#')
		{
			const long cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X') ? strtol(ent.c_str() + 2, nullptr, 16) : strtol(ent.c_str() + 1, nullptr, 10);
			if (cp > 0 && cp < 0x80)
				out += char(cp);
			else if (cp < 0x800)
			{
				out += char(0xC0 | (cp >> 6));
				out += char(0x80 | (cp & 0x3F));
			}
			else if (cp < 0x10000)
			{
				out += char(0xE0 | (cp >> 12));
				out += char(0x80 | ((cp >> 6) & 0x3F));
				out += char(0x80 | (cp & 0x3F));
			}
		}
		i = semi;
	}
	return out;
}

std::string Field(const std::string& body, const char* tag)
{
	const std::string open = std::string("<") + tag + ">";
	const size_t a = body.find(open);
	if (a == std::string::npos)
		return "";
	const size_t b = body.find(std::string("</") + tag + ">", a);
	if (b == std::string::npos)
		return "";
	std::string v = XmlText(body.substr(a + open.size(), b - a - open.size()));
	for (char& c : v)
		if (c == '\t' || c == '\n' || c == '\r')
			c = ' ';
	while (!v.empty() && v.back() == ' ')
		v.pop_back();
	while (!v.empty() && v.front() == ' ')
		v.erase(v.begin());
	return v;
}

struct LbList
{
	std::vector<std::string> names; // "Name (Region)"
	std::vector<std::string> files;
	std::vector<std::string> regions;
	artmatch::Index ix; // made once
};
struct LbIndex
{
	LbList kinds[4]; // front, back, spine, disc (3.10)
};
std::mutex g_lb_lock;
std::string g_lb_path;
long long g_lb_stamp = -1;
std::shared_ptr<LbIndex> g_lb;

int KindIndex(const std::string& kind)
{
	return kind == "front" ? 0 : kind == "back" ? 1 : kind == "spine" ? 2 : kind == "disc" ? 3 : -1;
}

std::shared_ptr<LbIndex> LoadIndex(const std::string& path)
{
	struct stat st = {};
	if (stat(path.c_str(), &st) != 0)
		return nullptr;
	const long long stamp = (long long)st.st_size * 1000003LL + (long long)st.st_mtime;
	std::lock_guard<std::mutex> l(g_lb_lock);
	if (g_lb && g_lb_path == path && g_lb_stamp == stamp)
		return g_lb;
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return nullptr;
	std::string text(size_t(std::max<long long>(0, st.st_size)), '\0');
	const bool ok = !text.empty() && fread(&text[0], 1, text.size(), f) == text.size();
	fclose(f);
	auto ix = std::make_shared<LbIndex>();
	if (ok && text.compare(0, strlen(kLaunchBoxFormat), kLaunchBoxFormat) == 0)
		Lines(text.data(), text.data() + text.size(), [&](const std::string& line) {
			const std::vector<std::string> v = Split(line, '\t');
			if (v.size() < 4 || v[0].empty() || v[2].empty())
				return;
			const int k = KindIndex(v[3]);
			if (k < 0)
				return;
			ix->kinds[k].names.push_back(WithRegion(v[0], RegionWord(v[1])));
			ix->kinds[k].regions.push_back(v[1]);
			ix->kinds[k].files.push_back(v[2]);
		});
	for (LbList& k : ix->kinds)
		artmatch::Build(k.ix, k.names);
	g_lb = ix;
	g_lb_path = path;
	g_lb_stamp = stamp;
	return ix;
}
} // namespace

bool SerialTitle(const std::string& product, std::string* title, std::string* region)
{
	const std::string n = Norm(product);
	if (n.size() < 3)
		return false;
	const auto& t = Serials();
	auto it = t.find(n);
	if (it == t.end())
		return false;
	if (title)
		*title = it->second.title;
	if (region)
		*region = it->second.region;
	return true;
}

std::string PreferredRegion(const Disc& d)
{
	// the areas the disc itself says: one region alone is sure
	const bool j = d.areas.find('J') != std::string::npos, u = d.areas.find('U') != std::string::npos,
			   e = d.areas.find('E') != std::string::npos;
	if (j + u + e == 1)
		return j ? "Japan" : u ? "USA" : "Europe";
	std::string title, region;
	if (SerialTitle(d.product, &title, &region))
	{
		const std::string r = RegionWord(region);
		if (!r.empty())
			return r;
		const std::string tag = artmatch::Parse(title).region;
		if (!tag.empty())
			return tag;
	}
	for (const std::string& n : d.names)
	{
		const std::string tag = artmatch::Parse(n).region;
		if (!tag.empty())
			return tag;
	}
	return u ? "USA" : j ? "Japan" : e ? "Europe" : "";
}

std::vector<std::string> LookupNames(const Disc& d)
{
	std::string title;
	if (SerialTitle(d.product, &title, nullptr))
	{
		// "Virtua Fighter 2 (Korea)", "Sakura Taisen 2 (Disc 3)": the tags go (the region is told apart), the title stays
		const std::string plain = artmatch::Parse(title).title;
		return {plain.empty() ? title : plain};
	}
	std::vector<std::string> out;
	for (const std::string& n : d.names)
	{
		const std::string plain = artmatch::Parse(n).title;
		if (!plain.empty() && std::find(out.begin(), out.end(), plain) == out.end())
			out.push_back(plain);
	}
	return out;
}

std::vector<Candidate> ChromagiCandidates(const Disc& d)
{
	std::vector<Candidate> out;
	const auto& rows = Chromagi();
	auto add = [&](const ChromagiRow& r, const char* why) {
		for (const char* kind : {"boxback", "backcover"})
		{
			Candidate c;
			c.url = "https://chromagi.com/assets/ss/soft/" + r.slug + "/" + r.slug + "." + kind + ".avif";
			c.label = std::string("Chromagi: ") + r.title + (r.region.empty() ? "" : " (" + r.region + ")") + why;
			out.push_back(c);
		}
	};
	// 1. a page that lists the disc's own number
	const std::string n = Norm(d.product);
	if (n.size() >= 3)
		for (const ChromagiRow& r : rows)
			if (std::find(r.codes.begin(), r.codes.end(), n) != r.codes.end())
			{
				add(r, "");
				return out;
			}
	// 2. the same game, by title (the disc's region first)
	std::vector<std::string> names;
	for (const ChromagiRow& r : rows)
		names.push_back(WithRegion(r.title, r.region));
	const std::string region = PreferredRegion(d);
	const int hit = Match(names, LookupNames(d), region);
	if (hit >= 0)
	{
		// the game's other pages too (another region's), the wanted region first
		const std::string key = artmatch::TitleKey(names[size_t(hit)]);
		add(rows[size_t(hit)], "");
		for (size_t i = 0; i < rows.size(); i++)
			if (int(i) != hit && artmatch::TitleKey(names[i]) == key)
				add(rows[i], "");
	}
	return out;
}

bool LaunchBoxIndexValid(const std::string& index_path)
{
	FILE* f = fopen(index_path.c_str(), "rb");
	if (!f)
		return false;
	char head[64] = {};
	const size_t n = fread(head, 1, sizeof(head) - 1, f);
	fclose(f);
	return n >= strlen(kLaunchBoxFormat) && strncmp(head, kLaunchBoxFormat, strlen(kLaunchBoxFormat)) == 0;
}

std::vector<Candidate> LaunchBoxCandidates(const Disc& d, const std::string& index_path, const std::string& image_base,
	const std::string& kind)
{
	std::vector<Candidate> out;
	const std::shared_ptr<LbIndex> all = LoadIndex(index_path);
	const int k = KindIndex(kind);
	if (!all || k < 0 || all->kinds[k].names.empty())
		return out;
	const LbList* ix = &all->kinds[k];
	const std::string region = PreferredRegion(d);
	int hit = -1;
	for (const std::string& q : LookupNames(d))
	{
		const std::string name = q.empty() ? std::string() : artmatch::Resolve(ix->ix, q, region, false);
		if (name.empty())
			continue;
		for (size_t i = 0; i < ix->names.size() && hit < 0; i++)
			if (ix->names[i] == name)
				hit = int(i);
		if (hit >= 0)
			break;
	}
	if (hit < 0)
		return out;
	// every back cover of that game: the chosen one's region first, then the others
	const std::string key = artmatch::TitleKey(ix->names[size_t(hit)]);
	std::vector<size_t> same;
	for (size_t i = 0; i < ix->names.size(); i++)
		if (artmatch::TitleKey(ix->names[i]) == key)
			same.push_back(i);
	std::stable_sort(same.begin(), same.end(), [&](size_t a, size_t b) {
		auto rank = [&](size_t i) {
			if (i == size_t(hit))
				return 0;
			const std::string r = RegionWord(ix->regions[i]);
			return r == region ? 1 : r.empty() || r == "World" ? 2 : 3;
		};
		return rank(a) < rank(b);
	});
	std::vector<std::string> seen;
	for (size_t i : same)
	{
		if (std::find(seen.begin(), seen.end(), ix->files[i]) != seen.end())
			continue;
		seen.push_back(ix->files[i]);
		Candidate c;
		c.url = image_base + ix->files[i];
		c.label = "LaunchBox: " + ix->names[i];
		out.push_back(c);
	}
	return out;
}

// ---- LaunchBox's Metadata.xml ------------------------------------------------------------------------------------
void LaunchBoxParser::Feed(const char* data, size_t n)
{
	m_buf.append(data, n);
	size_t pos = 0;
	for (;;)
	{
		const size_t lt = m_buf.find("<Game", pos);
		if (lt == std::string::npos)
		{
			// keep a tail that could be the start of "<Game"
			pos = m_buf.size() > 8 ? m_buf.size() - 8 : 0;
			break;
		}
		const size_t gt = m_buf.find('>', lt);
		if (gt == std::string::npos)
		{
			pos = lt;
			break;
		}
		const std::string tag = m_buf.substr(lt + 1, gt - lt - 1);
		if (tag != "Game" && tag != "GameImage" && tag != "GameAlternateName")
		{
			pos = gt + 1;
			continue;
		}
		const std::string close = "</" + tag + ">";
		const size_t end = m_buf.find(close, gt);
		if (end == std::string::npos)
		{
			pos = lt;
			break;
		}
		Block(tag, m_buf.substr(gt + 1, end - gt - 1));
		pos = end + close.size();
	}
	m_buf.erase(0, pos);
}

void LaunchBoxParser::Block(const std::string& tag, const std::string& body)
{
	if (tag == "Game")
	{
		if (Field(body, "Platform") != "Sega Saturn")
			return;
		const int id = atoi(Field(body, "DatabaseID").c_str());
		const std::string name = Field(body, "Name");
		if (id > 0 && !name.empty())
			m_saturn.emplace_back(id, name);
	}
	else if (tag == "GameImage")
	{
		const std::string type = Field(body, "Type");
		// 3.10: "Disc" (a photo of the disc) and "Fanart - Disc" (a drawn one, transparent around it), for the launch
		// animation
		const char* kind = type == "Box - Back" ? "back" : type == "Box - Front" ? "front" : type == "Box - Spine" ? "spine"
			: type == "Disc" || type == "Fanart - Disc" ? "disc" : nullptr;
		if (!kind)
			return;
		const int id = atoi(Field(body, "DatabaseID").c_str());
		std::string file = Field(body, "FileName");
		if (id > 0 && !file.empty() && file.find("..") == std::string::npos && file.find('/') == std::string::npos)
			m_images.push_back({id, file, Field(body, "Region"), kind});
	}
	else
	{
		const int id = atoi(Field(body, "DatabaseID").c_str());
		const std::string name = Field(body, "AlternateName");
		if (id > 0 && !name.empty())
			m_alts.push_back({id, name, Field(body, "Region")});
	}
}

std::string LaunchBoxParser::Finish()
{
	std::unordered_map<int, std::string> saturn(m_saturn.begin(), m_saturn.end());
	std::unordered_map<int, std::vector<const Alt*>> alts;
	for (const Alt& a : m_alts)
		if (saturn.count(a.id))
			alts[a.id].push_back(&a);
	std::string out = std::string(kLaunchBoxFormat) + "\n"
					  "# SSYmirPS5: LaunchBox Games DB's Sega Saturn box art (Box - Front, Box - Back, Box - Spine) and discs, from its Metadata.zip\n"
					  "# name<TAB>region<TAB>picture (images.launchbox-app.com/<picture>)<TAB>front|back|spine|disc; delete this file to\n"
					  "# read the database again\n";
	size_t n = 0;
	for (const Image& im : m_images)
	{
		auto g = saturn.find(im.id);
		if (g == saturn.end())
			continue;
		out += g->second + "\t" + im.region + "\t" + im.file + "\t" + im.kind + "\n";
		n++;
		// the game's other names (a Japanese release is often listed under its English name), for the image's region
		for (const Alt* a : alts[im.id])
			if (a->region.empty() || im.region.empty() || RegionWord(a->region) == RegionWord(im.region))
				out += a->name + "\t" + im.region + "\t" + im.file + "\t" + im.kind + "\n";
	}
	(void)n;
	return out;
}

int BuildLaunchBoxIndex(const std::string& metadata_zip, const std::string& index_path)
{
	unzFile z = unzOpen64(metadata_zip.c_str());
	if (!z)
	{
		OrbisLog("[backs] %s is not a zip", metadata_zip.c_str());
		return -1;
	}
	int found = -1;
	if (unzLocateFile(z, "Metadata.xml", 2) == UNZ_OK && unzOpenCurrentFile(z) == UNZ_OK)
	{
		LaunchBoxParser p;
		std::vector<char> buf(1u << 20);
		int r;
		uint64_t total = 0;
		while ((r = unzReadCurrentFile(z, buf.data(), unsigned(buf.size()))) > 0)
		{
			p.Feed(buf.data(), size_t(r));
			total += uint64_t(r);
		}
		unzCloseCurrentFile(z);
		if (r == 0)
		{
			const std::string text = p.Finish();
			size_t lines = 0;
			for (char c : text)
				lines += c == '\n';
			found = int(lines) - 4;
			OrbisLog("[backs] LaunchBox: %llu MB of Metadata.xml read, %zu Saturn games, %d picture name(s)",
				(unsigned long long)(total >> 20), p.games(), found);
			if (p.games() == 0)
				found = -1;
			else if (!WriteFileAtomicTo(index_path, std::vector<uint8_t>(text.begin(), text.end())))
				found = -1;
		}
		else
			OrbisLog("[backs] LaunchBox: Metadata.xml could not be read (%d)", r);
	}
	else
		OrbisLog("[backs] LaunchBox: no Metadata.xml in %s", metadata_zip.c_str());
	unzClose(z);
	return found;
}
} // namespace backs
} // namespace fe
