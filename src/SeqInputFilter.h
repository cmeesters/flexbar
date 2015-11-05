/*
 *   SeqInputFilter.h
 *
 *   Authors: mat and jtr
 */

#ifndef FLEXBAR_SEQINPUTFILTER_H
#define FLEXBAR_SEQINPUTFILTER_H

#include <string>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <tbb/pipeline.h>

#include <seqan/file.h>
#include <seqan/stream.h>
#include <seqan/seq_io.h>

#include "Enums.h"
#include "Options.h"
#include "FlexbarIO.h"
#include "SeqRead.h"
#include "QualTrimming.h"


template <typename TSeqStr, typename TString, typename TStream>
class SeqInputFilter : public tbb::filter {

private:
	
	typedef seqan::RecordReader<TStream, seqan::SinglePass<> > TRecordReader;
	TRecordReader *reader;
	TStream fstrm;
	
	typedef seqan::RecordReader<std::istream, seqan::SinglePass<> > TRecordReaderCin;
	TRecordReaderCin *readerCin;
	
	// typedef seqan::String<char, seqan::MMap<> > TMMapString;
	// typedef seqan::RecordReader<TMMapString, seqan::SinglePass<seqan::StringReader> > TRecordReaderStr;
	// TRecordReaderStr *strReader;
	
	const flexbar::QualTrimType m_qtrim;
	flexbar::FileFormat m_format;
	TString m_nextTag;
	
	const bool m_switch2Fasta, m_preProcess, m_useStdin;
	const int m_maxUncalled, m_preTrimBegin, m_preTrimEnd, m_qtrimThresh, m_qtrimWinSize;
	
	tbb::atomic<unsigned long> m_nrReads, m_nrChars, m_nLowPhred;
	
public:
	
	SeqInputFilter(const Options &o, const std::string filePath, const bool fastaFormat, const bool preProcess, const bool useStdin) :
		
		filter(serial_in_order),
		m_preProcess(preProcess),
		m_useStdin(useStdin),
		m_switch2Fasta(o.switch2Fasta),
		m_maxUncalled(o.maxUncalled),
		m_preTrimBegin(o.cutLen_begin),
		m_preTrimEnd(o.cutLen_end),
		m_qtrim(o.qTrim),
		m_qtrimThresh(o.qtrimThresh),
		m_qtrimWinSize(o.qtrimWinSize),
		m_format(o.format){
		
		m_nextTag   = "";
		m_nrReads   = 0;
		m_nrChars   = 0;
		m_nLowPhred = 0;
		
		using namespace std;
		using namespace flexbar;
		
		if(fastaFormat){
			m_format = FASTA;
		}
		else if(m_switch2Fasta){
			m_format = FASTQ;
		}
		
		if(m_useStdin) readerCin = new TRecordReaderCin(cin);
		else{
			openInputFile(fstrm, filePath);
			reader = new TRecordReader(fstrm);
			// istream &f = fstrm;
		}
		
		// TMMapString mmapStr;
		// if(! open(mmapStr, filePath.c_str(), seqan::OPEN_RDONLY)){
		// cout << "Error opening File: " << filePath << endl; }
		// strReader = new TRecordReaderStr(mmapStr);
	};
	
	
	virtual ~SeqInputFilter(){
		
		if(m_useStdin) delete readerCin;
		else{
			delete reader;
			closeFile(fstrm);
		}
	};
	
	
	unsigned long getNrLowPhredReads() const {
		return m_nLowPhred;
	}
	
	
	unsigned long getNrProcessedReads() const {
		return m_nrReads;
	}
	
	
	unsigned long getNrProcessedChars() const {
		return m_nrChars;
	}
	
	
	bool atStreamEnd(){
		if(m_useStdin) return atEnd(*readerCin);
		else           return atEnd(*reader);
	}
	
	
	void readOneLine(seqan::CharString &text){
		using namespace std;
		
		text = "";
		
		if(! atStreamEnd()){
			
			if(m_useStdin){
				if(readLine(text, *readerCin) != 0){
					cerr << "File reading error occured.\n" << endl;
					exit(1);
				}
			}
			else{
				if(readLine(text, *reader) != 0){
					cerr << "File reading error occured.\n" << endl;
					exit(1);
				}
			}
		}
	}
	
	
	// returns single SeqRead or NULL if no more reads in file or error
	
	void* getRead(bool &isUncalled){
		
		using namespace std;
		using namespace flexbar;
		
		using seqan::prefix;
		using seqan::suffix;
		using seqan::length;
		
		SeqRead<TSeqStr, TString> *myRead = NULL;
		
		TSeqStr source = "";
		TString tag = "", quality = "", dummy = "";
		
		if(! atStreamEnd()){
			
			isUncalled = false;
			
			try{
				if(m_format == FASTA){
					
					// tag line is read in previous iteration
					if(m_nextTag == "") readOneLine(tag);
					else                tag = m_nextTag;
					
					if(length(tag) > 0){
						if(getValue(tag, 0) != '>'){
							stringstream error;
							error << "Incorrect FASTA entry: missing > symbol for " << tag << endl;
							throw runtime_error(error.str());
						}
						else tag = suffix(tag, 1);
						
						if(length(tag) == 0){
							stringstream error;
							error << "Incorrect FASTA entry: missing read name after > symbol." << endl;
							throw runtime_error(error.str());
						}
					}
					else return NULL;
					
					
					readOneLine(source);
					
					if(length(source) < 1){
						stringstream error;
						error << "Empty FASTA entry: found tag without read for " << tag << endl;
						throw runtime_error(error.str());
					}
					
					
					readOneLine(m_nextTag);
					
					// fasta files with sequences on multiple lines
					while(! atStreamEnd() && length(m_nextTag) > 0 && getValue(m_nextTag, 0) != '>'){
						append(source, m_nextTag);
						readOneLine(m_nextTag);
					}
					
					m_nrChars += length(source);
					
					
					if(m_preProcess){
						isUncalled = isUncalledSequence(source);
						
						if(m_preTrimBegin > 0 && length(source) > 1){
							
							int idx = m_preTrimBegin;
							if(idx >= length(source)) idx = length(source) - 1;
							
							erase(source, 0, idx);
						}
						
						if(m_preTrimEnd > 0 && length(source) > 1){
							
							int idx = m_preTrimEnd;
							if(idx >= length(source)) idx = length(source) - 1;
							
							source = prefix(source, length(source) - idx);
						}
					}
					
					myRead = new SeqRead<TSeqStr, TString>(source, tag);
					
					++m_nrReads;
				}
				
				// FastQ
				else{
					
					readOneLine(source);
					
					if(length(source) > 0){
						if(getValue(source, 0) != '@'){
							stringstream error;
							error << "Incorrect FASTQ entry: missing @ symbol for " << source << endl;
							throw runtime_error(error.str());
						}
						else tag = suffix(source, 1);
						
						if(length(tag) == 0){
							stringstream error;
							error << "Incorrect FASTQ entry: missing read name after @ symbol." << endl;
							throw runtime_error(error.str());
						}
					}
					else return NULL;
					
					readOneLine(source);
					
					if(length(source) < 1){
						stringstream error;
						error << "Empty FASTQ entry: found tag without read for " << tag << endl;
						throw runtime_error(error.str());
					}
					
					
					readOneLine(dummy);
					
					if(length(dummy) == 0 || seqan::isNotEqual(getValue(dummy, 0), '+')){
							stringstream error;
							error << "Incorrect FASTQ entry: missing + line for " << tag << endl;
							throw runtime_error(error.str());
					}
					
					readOneLine(quality);
					
					if(length(quality) < 1){
						stringstream error;
						error << "Empty FASTQ entry: found read without quality values for " << tag << endl;
						throw runtime_error(error.str());
					}
					
					m_nrChars += length(source);
					
					
					if(m_preProcess){
						isUncalled = isUncalledSequence(source);
						
						if(m_preTrimBegin > 0 && length(source) > 1){
							
							int idx = m_preTrimBegin;
							if(idx >= length(source)) idx = length(source) - 1;
							
							erase(source, 0, idx);
							erase(quality, 0, idx);
						}
						
						if(m_preTrimEnd > 0 && length(source) > 1){
							
							int idx = m_preTrimEnd;
							if(idx >= length(source)) idx = length(source) - 1;
							
							source  = prefix(source,  length(source)  - idx);
							quality = prefix(quality, length(quality) - idx);
						}
						
						if(m_qtrim != QOFF) qualityTrimming(source, quality);
					}
					
					if(m_switch2Fasta) myRead = new SeqRead<TSeqStr, TString>(source, tag);
					else               myRead = new SeqRead<TSeqStr, TString>(source, tag, quality);
					
					++m_nrReads;
				}
				
				return myRead;
			}
			catch(exception &e){
				cerr << "\n\n" << e.what() << "\nProgram execution aborted.\n" << endl;
				
				if(m_useStdin) delete readerCin;
				else{
					delete reader;
					closeFile(fstrm);
				}
				exit(1);
			}
		}
		
		// end of stream
		else return NULL;
	}
	
	
	// returns TRUE if read contains too many uncalled bases
	bool isUncalledSequence(TSeqStr &source){
		int n = 0;
		
		using namespace seqan;
		
		typename Iterator<TSeqStr >::Type it, itEnd;
		
		it    = begin(source);
		itEnd = end(source);
		
		while(it != itEnd){
			 if(*it == 'N') n++;
			 ++it;
		}
		
		return(n > m_maxUncalled);
	}
 	
	
	void qualityTrimming(TSeqStr &source, TString &quality){
		
		using namespace seqan;
		
		unsigned cut_pos = qualTrim(quality, m_qtrim, m_qtrimThresh, m_qtrimWinSize);
		
		if(cut_pos < length(quality)){
			
			++m_nLowPhred;
			
			source  = prefix(source,  cut_pos);
			quality = prefix(quality, cut_pos);
		}
	}
	
	
	// override
	void* operator()(void*){
		
		bool isUncalled = false;
		return getRead(isUncalled);
	}
	
};

#endif