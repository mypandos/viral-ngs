//========================================================================
// Project     : VariantCaller
// Name        : bam_manip.cpp
// Author      : Xiao Yang
// Created on  : Apr 9, 2012
// Version     : 1.0
// Copyright Broad Institute, Inc. 2013.
// Notice of attribution: The V-Phaser 2.0 program was made available through the generosity of Genome Sequencing and Analysis Program at the Broad Institute, Inc. per Yang X, Charlebois P, Macalalad A, Henn MR and Zody MC (2013) V-Phaser 2.0: Variant Inference for Viral Populations” See accompanying file LICENSE_1_0.txt.  Distribution subject to licenses from Boost Software and MIT (http://www.boost.org/LICENSE_1_0.txt and https://github.com/pezmaster31/bamtools/blob/master/LICENSE).

// Description :
//========================================================================

#include "bam_manip.h"
#include <ctime>
#include <limits.h>

/**********************************************************************
 * @brief	parse bam headers, check if bam file is sorted,
 * 			get ref information as a global parameter [gParam.ref]
 *			Each ref has the following structure:
 *				Name --> {
 *					- sequence
 *					- (fileID_1, seqID_1) (fileID_2, seqID_2) ...
 *				}
 *
 * @param	[gParam]: global parameters
 *
 **********************************************************************/
void parse_bam_header (GlobalParam& gParam, int& pSample) {

	BamTools::BamReader reader;

	/* for each bam file */
	for (int i = 0; i < (int) gParam.bam_filelist.size(); ++ i) {
		if (!reader.Open(gParam.bam_filelist[i])) {
			abording ("Failed to open: " + gParam.bam_filelist[i]);
		}

		BamTools::SamHeader header = reader.GetHeader();

		/********************** Header ***********************/
		/* sanity check if bam file is sorted "SO:coordinate" */
		if (header.SortOrder.compare("coordinate") != 0) { // @SO
			std::cout << "\tInput BAM file should be sorted by coordinate\n\n";
			warning (gParam.bam_filelist[i] + " sort order: " + header.SortOrder);
		}

		/* get reference names and bam file IDs each ref belong to */
		if (header.HasSequences()){ // @SQ
			BamTools::SamSequenceIterator it = header.Sequences.Begin();
			for (; it != header.Sequences.End(); ++ it) {

				if (it->HasName()) {
					Ref::iterator it_ref = gParam.reference.find(it->Name);
					if (it_ref == gParam.reference.end()){
						std::cout << "\t" << it->Name << " len =" << it->Length << "\n";
						gParam.reference[it->Name] =
						 RefContent (string_to<int>(it->Length), i,
								 reader.GetReferenceID(it->Name));
					} else {
						it_ref->second.bfID_refID.push_back(
							ipair_t(i, reader.GetReferenceID(it->Name)));
					}
				}
			}// for (;
		} // if

		// --------- PRINT OUT ------------
		std::cout << "\n\t" << gParam.reference.size()
				<< " ref sequence(s) found: \n";
		Ref::iterator it_ref = gParam.reference.begin();
		for (; it_ref != gParam.reference.end(); ++ it_ref){
			std::cout << "\t\tName: " << it_ref->first << "\n";
			for (int i = 0; i < (int) it_ref->second.bfID_refID.size();
					++ i) {
				std::cout << "\t\t\tBamfileID = "
						<< it_ref->second.bfID_refID[i].first << "\t"
						<< "RefID = "
						<< it_ref->second.bfID_refID[i].second << "\n";
			}
		}
		std::cout << "\n";

		/* check platform */
		std::set<std::string> platforms;
		jaz::to_upper toupperFuctor;
		if(header.HasReadGroups()){ // @RG
			BamTools::SamReadGroupIterator it = header.ReadGroups.Begin();
			for (; it != header.ReadGroups.End(); ++ it) {
				// sanity check platform
				if (it->HasSequencingTechnology()) {
					platforms.insert(toupperFuctor(it->SequencingTechnology));
				}
			}
		} // if

		// --------- PRINT OUT ------------
		std::cout << "\n\t" << platforms.size() << " platform(s) found: ";
		std::set<std::string>::const_iterator it = platforms.begin();
		for (; it != platforms.end(); ++ it) {
			std::cout << (*it) << "\t";
			if (toupperFuctor(*it).compare("ILLUMINA") != 0) pSample = 100;
		}
		std::cout << "\n\n";

		reader.Close();
	}// for

	/* set up global id for each reference */
	/*
	int global_id = 0;
	Ref::iterator it_ref = gParam.reference.begin();
	for (; it_ref != gParam.reference.end(); ++ it_ref) {
		it_ref->second.global_id = global_id;
		++ global_id;
	}*/
} // parse_bam_header



/* @brief	Fetch reference sequences from fasta file and store
 * 			sequence into global param [reference]
 */
bool fetch_ref_seq (Ref& reference, const char* filepath)  {
    std::ifstream f(filepath);
    bIO::fasta_input_iterator<> ii(f), end;
    for (; ii != end; ++ii) {
    		Ref::iterator it_ref = reference.find(ii->first);
    		if (it_ref == reference.end()) {
    			std::cout << "\tNo mapping to reference: " << ii->first <<"\n";
    		} else {
    			it_ref->second.seq = ii->second;
    		}
    }
    return (f.eof());
} // fetch_ref_seq

/*	@brief	Generate quality score --> quantile map
 *
 * 	@param [quantile]: number of quality score quantiles
 * 	@param [minQ]: min quality score observed in the input data
 */
void qqMap (imap_t& qq, int minQ, int maxQ, int quantile) {
	int size =  maxQ - minQ + 1;
	ivec_t qArray (size, minQ);
	for (int i = 0; i < (int) qArray.size(); ++ i) qArray[i] += i;

	if (quantile >= size) {
		for (int i = 0; i < size; ++ i) {
			qq[qArray[i]] = i;
		}
	} else {
		int avg = size/quantile, rem = size - avg * quantile;
		ivec_t binSize (quantile, avg);
		int idx = 1;
		while (rem) {
			++ binSize[quantile - idx];
			-- rem;
			++ idx;
		}
		int index_qArray = 0;
		for (int i = 0; i < quantile; ++ i) {
			for (int j = 0; j < binSize[i]; ++ j, ++index_qArray) {
				qq[qArray[index_qArray]] = i;
			}
		}
	}
	/*
	{ //debug print
		imap_t::iterator it = qq.begin();
		for (; it != qq.end(); ++ it) {
			std::cout << it->first << "\t" << it->second << "\n";
		}
	} */
} //qqMap

/* @brief	Get the max and min quality scores, max read length,	and
 * 			mean & std of fragment size in the input	by sampling
 * 			P% reads
 */
void sampling (int& minQ, int& maxQ, int& maxRL, int& avgfragSz,
		int& stdfragSz, const strvec_t& filelist, int P) {

	srand ( time(NULL) );
	minQ = INT_MAX;
	maxQ = INT_MIN;
	maxRL = INT_MIN;

	ivec_t fragSz; // sampling distance between start positions of read-pairs
	int cnt_total = 0, cnt_mapped = 0, cnt_chosen = 0;

	for (int i = 0; i < (int) filelist.size(); ++ i) {
		BamReader reader;
		if (!reader.Open(filelist[i])) {
			abording ("Failed to open: " + filelist[i]);
		}

		SamHeader header = reader.GetHeader();

		// used to calculate fragment size and sdv
		strset_t rNames;
		strset_t::iterator it_rn;
		/********************** Alignments ***********************/
		BamAlignment al;

		while (reader.GetNextAlignment(al)) {
			cnt_total ++;
			if (al.IsMapped()) {
				cnt_mapped ++;

				it_rn = rNames.find(al.Name);
				if (rand() % 100 < P) {

					if (it_rn == rNames.end()) {
						if (al.MatePosition != -1) {
							rNames.insert(al.Name);
							if (al.Position > al.MatePosition) {
								fragSz.push_back(std::abs(
										al.Position - al.MatePosition));
							}
						}
					} else rNames.erase(it_rn);

					cnt_chosen ++;
					al.BuildCharData();
					for (int i = 0; i < (int) al.Qualities.size(); ++ i) {
						if ( (int) al.Qualities[i] > maxQ ) {
							maxQ = (int) al.Qualities[i];
						} else if ((int) al.Qualities[i] < minQ ){
							minQ = (int) al.Qualities[i];
						}
					} // for
					if (al.Length > maxRL) maxRL = al.Length;

					//debug print out
					//std::cout << al.Position << "\t" << al.MatePosition
					//		<< "\t" << al.InsertSize << "\n";
					/*
					std::cout << al.QueryBases << "\n\n";
					std::cout << al.AlignedBases << "\n\n";
					int cigar_len = 0;
					for (int i = 0; i < al.CigarData.size(); ++ i) {
						std::cout << al.CigarData[i].Type << al.CigarData[i].Length;
						cigar_len += al.CigarData[i].Length;
					}
					std::cout << "\t\t" << cigar_len << "\n";
					if (cnt_mapped > 200)
						exit(1);
					*/
				} // if (rand() % 100 < P)
			} // if (al.IsMapped())
		} // while

		/*
		{ // debug print
			std::cout << "fragSz = \t" << fragSz.size() << "\n";
			for (int i = 0; i < (int) fragSz.size(); ++ i) {
				std::cout << fragSz[i] << "\t";
				if (i % 15 == 0) std::cout << "\n";
			}
			std::cout << "\n";
		}*/

		reader.Close();
	}

	/* calculate mean frag sz and std */
	int sample_sz = fragSz.size();
	uint64_t sum = 0;
	for (int i = 0; i < sample_sz; ++ i) {
		sum += fragSz[i];
	}
	if (sum > 0 && sample_sz > 0) {
		avgfragSz = sum/sample_sz;
	}
	sum = 0;
	for (int i = 0; i < (int) fragSz.size(); ++ i) {
		sum += (avgfragSz - fragSz[i])*(avgfragSz - fragSz[i]);
	}
	if (sample_sz > 1) {
		stdfragSz = std::sqrt(sum/sample_sz - 1);
	}

	std::cout << "\tTotal Reads = " << cnt_total << "\n"
			  << "\t# Mapped Reads = " << cnt_mapped << "\n"
			  << "\t# Reads used for checking Q scores = "
			  << cnt_chosen << "\n"
			  << "\tminQ = " << minQ << "\tmaxQ=" << maxQ << "\t"
			  << "\tmaxRL = " << maxRL << "\n";
	std::cout << "\t(avgfragSz, std) = " << avgfragSz << "\t"
			  << stdfragSz << "\n";
	if (sample_sz == 0) {
		warning ("Apparently the BAM file is not set properly such that "
				"the Fragment length cannot be measured IF this is Illumina "
				"paired alignment file");
	}

} // sampling


/* @brief	Given an alignment entry of read, calculate its end
 * 			position of mapping on ref.
 */
int calculate_aln_end (const BamTools::BamAlignment& al) {
	int pos = al.Position;
	for (int i = 0; i < (int) al.CigarData.size(); ++ i) {
		switch (al.CigarData[i].Type) {
		case 'M':
		case '=':
		case 'X':
		case 'D':
		case 'N':
			pos += al.CigarData[i].Length;
			break;
		default:
			break;
		} // switch
	}
	return (pos - 1);
} // calculate_end_pos

/* @brief	Update alnMap for read [rName], given [fileId], [refId] in this file,
 * 			[start_on_ref] and [end_on_ref].
 */
void udpate_alnMap (std::map<std::string, MapEntry>& alnMap,
		int fileId, const BamTools::BamAlignment& al) {

	int end_on_ref = calculate_aln_end (al);

	std::map<std::string, MapEntry>::iterator it = alnMap.find(al.Name);

	if (it != alnMap.end()) {
	    // matepairs map to the same refId of the same file
		if (fileId == it->second.first.fileID &&
			al.RefID == it->second.first.refID) {

			if (al.Position >= it->second.first.start) {
				it->second.second =  MapRecord (al.Position, end_on_ref,
						fileId, al.RefID, al.IsFirstMate());
			} else {
				it->second.second = it->second.first;
				it->second.first = MapRecord (al.Position, end_on_ref,
									fileId, al.RefID, al.IsFirstMate());
			}
		} else {
			// we can modify this part of code to tolerate this mapping
			// issue but removing such read pairs from consideration.

			std::cout << "Second found read: " << al.Name << "\t";
			std::cout << "(fileID, refID) = (" << fileId << "," << al.RefID
					<<	") is inconsistent with First found read\n"
					<<  it->first << "\t(fileID, refID) = ("
					<< (int) it->second.first.fileID << ","
					<< (int) it->second.first.refID  << ")\n";

			abording ("update_alnMap: inconsistent fID & RefID, PE mapped"
					" to different ref genomes in BAM file -- you may use:"
					"{ samtools view -b -f 2 input.bam > output.bam } to fix"
					"this problem");
		}
	} else {

		alnMap[al.Name] = MapEntry (MapRecord(al.Position, end_on_ref,
				fileId, al.RefID, al.IsFirstMate()), MapRecord());

	}

} //update_alnMap

/**********************************************************************
 * @brief	Setting the alnMap array, recording paired read mapping info
 * 			in global parameter
 * @param	[gParam]: global parameters
 **********************************************************************/
void set_rmap_array (GlobalParam& gParam, const Parameter& myPara) {

	// process alignments
	int total_mapped_reads = 0;
	int cnt_mateMapped = 0;

	bool is_anyMateMapped = false; // flag indicating if there are mapped
								   // mate pairs in the input bam

	BamTools::BamReader reader;
	for (int i = 0; i < (int) gParam.bam_filelist.size(); ++ i) {

		if (!reader.Open(gParam.bam_filelist[i])) {
			abording ("Failed to open: " + gParam.bam_filelist[i]);
		}

		/********************** Alignments ***********************/

		BamTools::BamAlignment al;
		while (reader.GetNextAlignment(al)) {

			if (al.IsMapped()) {

				// a) according to refID, identify which ref it maps to
				bool is_findRef = false;
				Ref::iterator it_ref = gParam.reference.begin();
				for (; it_ref != gParam.reference.end(); ++ it_ref) {
					for (int j = 0;
					  j < (int) it_ref->second.bfID_refID.size(); ++ j) {

						if (it_ref->second.bfID_refID[j] ==
								ipair_t (i, al.RefID)){
							is_findRef = true;
							break;
						}
					}
					if (is_findRef) break;
				}
				if (is_findRef == false) {
					abording ("set_arrays: Couldn't find ref");
				}

				// b) fill in the global variable [alnMap]
				if (al.IsMateMapped()) {
					is_anyMateMapped = true;
					cnt_mateMapped ++;
				}
				udpate_alnMap (gParam.alnMap, i, al);

				++ total_mapped_reads;
			}
		}

		reader.Close();
	}


	/* there is no mapped mate pair, e.g. 454, then clear out alnMap */
	if (is_anyMateMapped == false) gParam.alnMap.clear();

	std::cout << "\t# total mapped reads: " << total_mapped_reads << "\n";
	std::cout << "\t# mapped mate-pairs = " << cnt_mateMapped/2 << "\n";

} // set_rmap_array


/* @brief	Gather alignments (to be stored in vector [alns]) overlapping
 * 			a particular region [start, end] of a given reference
 * 			sequence [it_ref]. The ref sequence may present in multiple
 * 			bam files.
 *
 * @param	[it_ref]:	the iterator of the reference interested in
 * 			[start] : 	start position of window
 */
void gather_alignments (std::vector<BamAlignment>& alns,
		Ref::const_iterator& it_ref, int start, int end,
		const GlobalParam& gParam, const Parameter& myPara) {

	// clear the vector first
	alns.clear();
	// due to a bug in Bamtools BamReader.Close () is not properly implemented,
	// we have to use another variable reader2 to reopen the file etc.
	std::string refName = it_ref->first;

	// go through every bamfile [refName] belongs to
	for (int i = 0; i < (int) it_ref->second.bfID_refID.size(); ++ i) {

		int bamfileID = it_ref->second.bfID_refID[i].first;
		int refID = it_ref->second.bfID_refID[i].second;

		BamReader reader;
		// open bam file
		if (!reader.Open(gParam.bam_filelist[bamfileID])) {
			abording ("gather_alignments: Failed to open file "
					+ gParam.bam_filelist[i]);
		}

		// locate index, if fail, try create then redo locate index
		if (!reader.LocateIndex(BamIndex::BAMTOOLS)) {
			if (!reader.CreateIndex(BamIndex::BAMTOOLS)) {
			//if (!reader.CreateIndex(BamIndex::STANDARD)) {

				abording ("gather_alignments: Failed to create index of file "
							+ gParam.bam_filelist[i]);
			} else { // close, reopen the file and relocate index
				if (!reader.Close() || !reader.Open(gParam.bam_filelist[bamfileID])) {
					abording ("Due to an unknown bug in library Bamtools, after"
							"creating bti index for Bam file, program failed to continue;"
							"Solution: just re-launch the program\n");
				}
				if (!reader.LocateIndex(BamIndex::BAMTOOLS)) {
					abording ("gather_alignments: Failed to locate bam "
							"index of file " + gParam.bam_filelist[i]);
				}
			}
		}

		// set bam region
		BamRegion region(refID, start, refID, end);
		if (!reader.SetRegion(region)) {
			abording ("gather_alignments: Failed to set region for reference "
					+ refName + " in file " + gParam.bam_filelist[i]);
		}

		// get all alignments overlapping with current window
		// and store in the vector
		BamAlignment al;
		while ( reader.GetNextAlignment(al) ) {

			if (al.IsMapped()) { // only store those mapped reads
				alns.push_back(al);
			}
		}

		reader.Close();

	} // for (int i = 0;

} // gather_alignments;

/* @brief	For each input alignment, if it is rvc mapped, reverse
 * 			computer raw CigarData and read, store in [rvc_alns]
 *
 */
void compute_rvc_alns (std::vector<rvc_aln_t>& rvc_alns,
		const std::vector<BamAlignment>& alns) {

	rvc_alns.resize(alns.size());
	for (int i = 0; i < (int) alns.size(); ++ i) {
		if (alns[i].IsReverseStrand()) {
			rvc_alns[i].raw_read = xny::get_rvc_str(alns[i].QueryBases);
			rvc_alns[i].raw_qual = std::string(alns[i].Qualities.rbegin(),
					alns[i].Qualities.rend());
		}
	}
} // compute_rvc_alns


void get_aln_column (std::vector<AlnColEntry>& cur_col, int ref_pos,
	int ignbases, const std::vector<BamAlignment>& alns,
	const ivec_t& aln_ends, const imap_t& qq) {

	cur_col.clear();
	int sz = alns.size();
	strvec_t debug_names;

	for (int i = 0; i < sz; ++ i) {

		AlnColEntry col_entry;
		// current read overlaps with the ref_pos
		if (ref_pos >= alns[i].Position + ignbases) {
			if (ref_pos <= aln_ends[i] - ignbases) {

				get_column_x (col_entry, ref_pos, alns[i], aln_ends[i], qq);

				if (!col_entry.dt.empty()) {
					col_entry.idx_aln_array = i;
					cur_col.push_back(col_entry);
					debug_names.push_back(alns[i].Name);
				}
			}
		} else break;
	}

	/*
	{ // print out content
		if (ref_pos == 2008){
			std::cout << "\nref_pos = " << ref_pos << "\n\n";
			for (int i = 0; i < cur_col.size(); ++ i) {
				std::cout << debug_names[i] << ":" << cur_col[i].dt
				<< "\t(" << cur_col[i].idx_aln_array
				<< " , " << cur_col[i].rcycle << " , " << cur_col[i].qt
				<< " , " << cur_col[i].flanking_base << " , " << cur_col[i].cons_type
				<< ")\n";
			}
		}
		//if (ref_pos > 499) exit(1);
	}*/

} // get_aln_column

/* @brief	Retrieve AlnColEntry wrt reference base [ref_pos] for a
 *			particular read, whose alignment information is stored in [al]
 *			Filter out any col entry satisfying one of the following:
 *			a) DD, b) ND, c) DN, d) ins containing N, e) flanking base is N
 *			N denotes a non-nucleotide base
 */
void get_column_x (AlnColEntry& col, int ref_pos, const BamAlignment& al,
		int end, const imap_t& qq) {
	jaz::to_upper upper;

    int rlen = al.QueryBases.length();
    col.is_rv = al.IsReverseStrand() ? true : false;
    col.is_firstMate = al.IsFirstMate();
    int idx_ref = al.Position;
    int idx_read = 0;
    char cur_base;
    for (int i = 0; i < (int) al.CigarData.size(); ++ i) {
        char c = al.CigarData[i].Type;
		int  l = al.CigarData[i].Length;
		switch (c) {
		case 'M':
		case '=':
		case 'X':
			if (idx_ref + l - 1 >= ref_pos){
				//col.idx_cigar = i;
				//col.cigar_pos = ref_pos - idx_ref;
				int cigar_pos = ref_pos - idx_ref;
				col.rcycle = idx_read + (ref_pos - idx_ref);

				imap_t::const_iterator it_q = qq.find (al.Qualities.at (col.rcycle));
				col.qt = (it_q == qq.end()) ? qq.rbegin()->second: it_q->second;

				cur_base = std::toupper(al.QueryBases.at (col.rcycle));
				// non-nt found
				if (!xny::is_nt(cur_base)) {
					col.dt.clear();
					return;
				}

				if (!col.is_rv) { // ---------fwd strand -------------------

					if (cigar_pos > 0) { // previous base is MX= as well
						col.dt += std::toupper(al.QueryBases.at (col.rcycle - 1));
						if (!xny::is_nt(*col.dt.rbegin())) {
							col.dt.clear();
							return;
						}
					} else { // prev base is NOT MX=
						if (col.rcycle > 0) { // cur_base is not the first in the read
							if (i - 1 < 0){
								abording ("get_column_x: pre cigar idx undefined");
							}
							char pre_cigar_type = al.CigarData[i - 1].Type;
							int pre_cigar_len = al.CigarData[i - 1].Length;
							switch (pre_cigar_type) {
							case 'H':
								col.dt += 'A'; // for hard clipping return 'A' to be prev base
								break;
							case 'S':
								col.dt += std::toupper(al.QueryBases.at
													(col.rcycle - 1));
								if (!xny::is_nt(*col.dt.rbegin())) {
									col.dt.clear();
									return;
								}
								break;
							case 'D':
							case 'N':	// f DxM
								col.dt += 'D';
								col.dt += to_string<int> (pre_cigar_len);
								col.flanking_base = std::toupper(al.QueryBases.at
										(col.rcycle - 1));
								if (!xny::is_nt(col.flanking_base)) {
									col.dt.clear();
									return;
								}
								break;
							case 'I': // IxM
								col.dt += 'I';
								col.dt += to_string<int>(pre_cigar_len);

								if (col.rcycle - pre_cigar_len - 1 < 0) {
									abording ("get_column_x: cigar idx b4 I undefined");
								}

								col.flanking_base = std::toupper(al.QueryBases.at
										(col.rcycle - pre_cigar_len - 1));

								col.cons_type = upper (al.QueryBases.substr(
										col.rcycle - pre_cigar_len, pre_cigar_len));

								if (!xny::is_nt(col.flanking_base)  ||
									!xny::is_nt_string(col.cons_type)) {
									col.dt.clear();
									return;
								}

								break;
							default:
								abording ("get_column_x: wrong pre_cigar_type: "
										+ pre_cigar_type);
								break;
							}// switch
						} // if
					}// else

					col.dt += cur_base;

				} else { //---------------- rvc strand -------------------------
					if (cigar_pos < l - 1){ // next base is also MX=
						col.dt += cur_base;
						col.dt += std::toupper(al.QueryBases.at (col.rcycle + 1));

						if (!xny::is_nt(*col.dt.rbegin())) {
							col.dt.clear();
							return;
						}

						//rvc and update rcycle
						col.dt = xny::get_rvc_str(col.dt);

					} else { // next base is a new cigar type
						if (col.rcycle == rlen - 1) { // last base already
							col.dt += cur_base;
							xny::rvc_str(col.dt);
						} else {
							if (i + 1 > (int) al.CigarData.size()){
								abording ("get_column_x: next cigar idx undefined");
							}
							char next_cigar_type = al.CigarData[i + 1].Type;
							int next_cigar_len = al.CigarData[i + 1].Length;
							switch (next_cigar_type) {
							case 'H':
								col.dt += cur_base;
								col.dt += 'A';
								xny::rvc_str(col.dt);
								break;
							case 'S':
								col.dt += cur_base;
								col.dt += std::toupper(al.QueryBases.at
										(col.rcycle + 1));

								if (!xny::is_nt(*col.dt.rbegin())) {
									col.dt.clear();
									return;
								}

								xny::rvc_str(col.dt);
								break;
							case 'N':
							case 'D': // f DxM
								col.dt += 'D';
								col.dt += to_string<int>(next_cigar_len);
								col.dt += xny::get_rvc_base(cur_base);
								col.flanking_base = std::toupper(xny::get_rvc_base(
										al.QueryBases.at	(col.rcycle + 1)));

								if (!xny::is_nt(col.flanking_base) ) {
									col.dt.clear();
									return;
								}

								break;
							case 'I': // IxM
									col.dt += 'I';
									col.dt += to_string<int>(next_cigar_len);
									col.dt += xny::get_rvc_base(cur_base);
									if (col.rcycle + next_cigar_len + 1 > rlen - 1){
										abording ("get_column_x: next cigar "
												"idx after I undefined");
									}
									col.flanking_base = std::toupper(xny::get_rvc_base(
										al.QueryBases.at (col.rcycle + next_cigar_len + 1)));
									col.cons_type = upper (al.QueryBases.substr(
											col.rcycle + 1, next_cigar_len));

									if (!xny::is_nt(col.flanking_base)  ||
										!xny::is_nt_string(col.cons_type)) {
										col.dt.clear();
										return;
									}

									xny::rvc_str(col.cons_type);
									break;
								default:
									abording ("get_column_x: wrong next_cigar_type: "
											+ next_cigar_type);
									break;
							} // switch

						}// else (not the last base)

					}// else

					col.rcycle = rlen - col.rcycle - 1;
				}// else rvc

				return;
			} // if (idx_ref + l - 1 >= ref_pos){

			idx_ref += l;
			idx_read += l;
			break;
		case 'D':
		case 'N':
			if (idx_ref + l - 1 >= ref_pos){
				col.rcycle = idx_read - 1;
				//col.idx_cigar = i;
				//col.cigar_pos = ref_pos - idx_ref;
				int cigar_pos = ref_pos - idx_ref;

				if (!col.is_rv) { // ---------fwd strand -------------------
					if (cigar_pos == 0) { // first D: MDx
						col.dt += std::toupper(al.QueryBases.at(col.rcycle));
						col.dt += 'D';
						col.dt += to_string<int>(l);
						if (col.rcycle + 1 > rlen - 1) {
							abording ("get_column_x: cigar idx after D undefined");
						}
						col.flanking_base = std::toupper(
								al.QueryBases.at(col.rcycle + 1));


						if (!xny::is_nt (col.dt.at(0))  ||
								!xny::is_nt(col.flanking_base)) {
							col.dt.clear();
							return;
						}

						imap_t::const_iterator it_q = qq.find (
								al.Qualities.at (col.rcycle + 1));
						col.qt = (it_q == qq.end()) ? qq.rbegin()->second
												    : it_q->second;
					} else { // DD
						col.dt.clear();
						return;
					}

				} else {// ---------rvc strand -------------------
					if (cigar_pos == 0) { // first D: MDx
						if (col.rcycle + 1 > rlen) {
							abording ("get_column_x: next rcycle out of range");
						}
						++ col.rcycle;
						col.dt += std::toupper(xny::get_rvc_base(
								al.QueryBases.at(col.rcycle)));
						col.dt += 'D';
						col.dt += to_string<int>(l);
						if (col.rcycle - 1 < 0) {
							abording ("get_column_x: cigar idx b4 D undefined");
						}
						col.flanking_base = std::toupper(xny::get_rvc_base(
								al.QueryBases.at(col.rcycle - 1)));

						imap_t::const_iterator it_q = qq.find (
								al.Qualities.at (col.rcycle - 1));
						col.qt = (it_q == qq.end()) ? qq.rbegin()->second
								                    : it_q->second;

						if (!xny::is_nt (col.dt.at(0))  ||
								!xny::is_nt(col.flanking_base)) {
							col.dt.clear();
							return;
						}

						col.rcycle = rlen - col.rcycle - 1;

					} else { // DD
						col.dt.clear();
						return;
					}
				}
				return;
			} // if (idx_ref + l - 1 >= ref_pos){

			idx_ref += l;
			break;
		case 'I':
		case 'S':
		case 'H':
			idx_read += l;
			break;
		case 'P':
			break;
		default:
			abording ("get_column_x: unknown cigar char found");
			break;
		} // switch
    } // for (int i =
    abording ("get_column_x: fail to identify mapping");
} // get_column_x

/* @brief	Given the current column and previous column,
 * 			Identify if there are any insertions before current column.
 * 			For this purpose, consider only fwd strand in cur column
 * 			and rvc strand in prev column.
 * 			Register inserted bases as [.cons_type] field
 */
void get_ins_column (std::vector<AlnColEntry>& ins_col,
		const std::vector<AlnColEntry>& cur_col,
		const std::vector<AlnColEntry>& prev_col) {

	bool ins_found = false;

	// consider current column
	for (int i = 0; i < (int) cur_col.size(); ++ i) {
		if (! cur_col[i].is_rv) { // consider forward strand only
			if (add_ins_entry (ins_col, cur_col[i])) ins_found = true;
		}
	} // for (int i = 0

	// consider previous column
	for (int i = 0; i < (int) prev_col.size(); ++ i) {
		if (prev_col[i].is_rv) { // consider rvc strand only
			if (add_ins_entry(ins_col, prev_col[i])) ins_found = true;
		}
	}
	if (!ins_found) ins_col.clear();
} // get_ins_column

/* @brief	Check if an aln column is prepended by Ins, if so, modify dt
 * 			to be the 'flanking base + first base in the insertion'
 * 			Return true if non-empty ins is found.
 * 			Omit entry with DxM or MDx
 */
bool add_ins_entry (std::vector<AlnColEntry>& col, const AlnColEntry& entry){
	AlnColEntry tmp = entry;
	if (tmp.cons_type.size()) {
		char m = *tmp.dt.rbegin();
		tmp.dt.clear();
		tmp.dt += entry.flanking_base;
		//tmp.dt += entry.cons_type.at(0);
		tmp.dt += m;
		col.push_back(tmp);
		return true;
	}
	// when ins, ignore begin or end of a read
	if (tmp.dt.size() > 1) {
		if (tmp.dt.at(0) != 'D' && tmp.dt.at(1) != 'D') {
			col.push_back(tmp);
		}
	}
	return false;
}

/* @brief	for every IxM entry, clear ins base [.cons_type] entry and
 * 			[.flanking_base] entry, and update [.dt] entry
 */
void clean_aln_column (std::vector<AlnColEntry>& col) {
	for (int i = 0; i < (int) col.size(); ++ i) {
		if (col[i].dt.at(0) == 'I') {
			char cur_base = *col[i].dt.rbegin(); // IxM: get M
			col[i].dt.clear();
			//col[i].dt += *col[i].cons_type.rbegin(); // IbbbM get the b next to M
			col[i].dt += col[i].flanking_base; // fIxM get fM
			col[i].dt += cur_base; // fM
			col[i].flanking_base = '\0';
			col[i].cons_type.clear();
		}
	} // for (int i = 0
} // clean_aln_column

void get_extended_cigar_string (cvec_t& ext_cigar,
		const std::vector<BamTools::CigarOp>& cigar_data) {

	for (int i = 0; i < (int) cigar_data.size(); ++ i) {
		cvec_t tmp (cigar_data[i].Length, cigar_data[i].Type);
		ext_cigar.insert(ext_cigar.end(), tmp.begin(), tmp.end());
	}
} // get_extended_cigar_string
