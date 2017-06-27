#include "../include/CSR.h"
#include "../include/COO.h"

#include <stdint.h>
#include <string>
#include <fstream>
#include <stdexcept>
#include <iterator>
#include <vector>
#include <algorithm>

namespace {
	struct CSRIOHeader
	{
		static constexpr char Magic[9] = { 'H', 'o', 'l', 'a', 1, 'C', 'S', 'R', 1 };

		char magic[sizeof(Magic)];
		uint64_t typesize;
		uint64_t num_rows, num_columns;
		uint64_t num_non_zeroes;

		CSRIOHeader() = default;


		template<typename T>
		static uint64_t typeSize()
		{
			return sizeof(T);
		}

		template<typename T>
		CSRIOHeader(const CSR<T>& mat)
		{
			for (size_t i = 0; i < sizeof(Magic); ++i)
				magic[i] = Magic[i];
			typesize = typeSize<T>();

			num_rows = mat.rows;
			num_columns = mat.cols;
			num_non_zeroes = mat.nnz;
		}

		bool checkMagic() const
		{
			for (size_t i = 0; i < sizeof(Magic); ++i)
				if (magic[i] != Magic[i])
					return false;
			return true;
		}
	};
}

template<typename T>
void CSR<T>::alloc(size_t r, size_t c, size_t n)
{
	rows = r;
	cols = c;
	nnz = n;

	data = std::make_unique<T[]>(n);
	col_ids = std::make_unique<unsigned int[]>(n);
	row_offsets = std::make_unique<unsigned int[]>(r+1);
}

template<typename T>
CSR<T> loadCSR(const char * file)
{
	std::ifstream fstream(file, std::fstream::binary);
	if (!fstream.is_open())
		throw std::runtime_error(std::string("could not open \"") + file + "\"");

	CSRIOHeader header;
	fstream.read(reinterpret_cast<char*>(&header), sizeof(CSRIOHeader));
	if (!fstream.good())
		throw std::exception("Could not read CSR header");
	if (!header.checkMagic())
		throw std::exception("File does not appear to be a CSR Matrix");

	if (header.typesize != CSRIOHeader::typeSize<T>())
		throw std::exception("File does not contain a CSR matrix with matching type");

	CSR<T> res;
	res.alloc(header.num_rows, header.num_columns, header.num_non_zeroes);

	fstream.read(reinterpret_cast<char*>(&res.data[0]), res.nnz * sizeof(T));
	fstream.read(reinterpret_cast<char*>(&res.col_ids[0]), res.nnz * sizeof(unsigned int));
	fstream.read(reinterpret_cast<char*>(&res.row_offsets[0]), (res.rows+1) * sizeof(unsigned int));

	if (!fstream.good())
		throw std::exception("Could not read CSR matrix data");

	return res;
}

template<typename T>
void storeCSR(const CSR<T>& mat, const char * file)
{
	std::ofstream fstream(file, std::fstream::binary);
	if (!fstream.is_open())
		throw std::runtime_error(std::string("could not open \"") + file + "\"");

	CSRIOHeader header(mat);
	fstream.write(reinterpret_cast<char*>(&header), sizeof(CSRIOHeader));

	fstream.write(reinterpret_cast<char*>(&mat.data[0]), mat.nnz * sizeof(T));
	fstream.write(reinterpret_cast<char*>(&mat.col_ids[0]), mat.nnz * sizeof(unsigned int));
	fstream.write(reinterpret_cast<char*>(&mat.row_offsets[0]), (mat.rows + 1) * sizeof(unsigned int));

}

template<typename T>
void spmv(DenseVector<T>& res, const CSR<T>& m, const DenseVector<T>& v, bool transpose)
{
	if (transpose && v.size != m.rows)
		throw std::runtime_error("SPMV dimensions mismatch");
	if (!transpose && v.size != m.cols)
		throw std::runtime_error("SPMV dimensions mismatch");

	size_t outsize = transpose ? m.cols : m.rows;
	if (res.size < outsize)
		res.data = std::make_unique<T[]>(outsize);
	res.size = outsize;

	if (transpose)
	{
		std::fill(&res.data[0], &res.data[0] + m.cols, 0);
		for (size_t i = 0; i < m.rows; ++i)
		{
			for (unsigned int o = m.row_offsets[i]; o < m.row_offsets[i+1]; ++o)
				res.data[m.col_ids[o]] += m.data[o] * v.data[i];
		}
	}
	else
	{
		for (size_t i = 0; i < m.rows; ++i)
		{
			T val = 0;
			for (unsigned int o = m.row_offsets[i]; o < m.row_offsets[i+1]; ++o)
				val += m.data[o] * v.data[m.col_ids[o]];
			res.data[i] = val;
		}
	}
}

template<typename T>
void convert(CSR<T>& res, const COO<T>& coo)
{
	struct Entry
	{
		unsigned r, c;
		T v;
		bool operator < (const Entry& other)
		{
			if (r != other.r) 
				return r < other.r;
			return c < other.c;
		}
	};

	std::vector<Entry> entries;
	entries.reserve(coo.nnz);
	for (size_t i = 0; i < coo.nnz; ++i)
		entries.push_back(Entry{ coo.row_ids[i], coo.col_ids[i], coo.data[i] });
	std::sort(std::begin(entries), std::end(entries));

	res.alloc(coo.rows, coo.cols, coo.nnz);
	std::fill(&res.row_offsets[0], &res.row_offsets[0], 0);
	for (size_t i = 0; i < coo.nnz; ++i)
	{
		res.data[i] = entries[i].v;
		res.col_ids[i] = entries[i].c;
		++res.row_offsets[entries[i].r];
	}

	unsigned int off = 0;
	for (size_t i = 0; i < coo.rows; ++i)
	{
		unsigned int n = off + res.row_offsets[i];
		res.row_offsets[i] = off;
		off = n;
	}
	res.row_offsets[coo.rows] = off;
}

template void CSR<float>::alloc(size_t, size_t, size_t);
template void CSR<double>::alloc(size_t, size_t, size_t);

template CSR<float> loadCSR(const char * file);
template CSR<double> loadCSR(const char * file);

template void storeCSR(const CSR<float>& mat, const char * file);
template void storeCSR(const CSR<double>& mat, const char * file);

template void spmv(DenseVector<float>& res, const CSR<float>& m, const DenseVector<float>& v, bool transpose);
template void spmv(DenseVector<double>& res, const CSR<double>& m, const DenseVector<double>& v, bool transpose);


template void convert(CSR<float>& res, const COO<float>& coo);
template void convert(CSR<double>& res, const COO<double>& coo);
