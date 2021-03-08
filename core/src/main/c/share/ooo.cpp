/*******************************************************************************
 *     ___                  _   ____  ____
 *    / _ \ _   _  ___  ___| |_|  _ \| __ )
 *   | | | | | | |/ _ \/ __| __| | | |  _ \
 *   | |_| | |_| |  __/\__ \ |_| |_| | |_) |
 *    \__\_\\__,_|\___||___/\__|____/|____/
 *
 *  Copyright (c) 2014-2019 Appsicle
 *  Copyright (c) 2019-2020 QuestDB
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/
#include "jni.h"
#include <cstring>
#include <xmmintrin.h>
#include "util.h"
#include "asmlib.h"

typedef struct {
    uint64_t c8[256];
    uint64_t c7[256];
    uint64_t c6[256];
    uint64_t c5[256];
    uint64_t c4[256];
    uint64_t c3[256];
    uint64_t c2[256];
    uint64_t c1[256];
} rscounts_t;

typedef struct index_t {
    uint64_t ts;
    uint64_t i;

    bool operator<(int64_t other) const {
        return ts < other;
    }

    bool operator>(int64_t other) const {
        return ts > other;
    }

    bool operator==(index_t other) const {
        return ts == other.ts;
    }
} index_t;

#define RADIX_SHUFFLE 0

#if RADIX_SHUFFLE == 0

template<uint16_t sh>
inline void radix_shuffle(uint64_t *counts, index_t *src, index_t *dest, uint64_t size) {
    _mm_prefetch(counts, _MM_HINT_NTA);
    for (uint64_t x = 0; x < size; x++) {
        const auto digit = (src[x].ts >> sh) & 0xffu;
        dest[counts[digit]] = src[x];
        counts[digit]++;
        _mm_prefetch(src + x + 64, _MM_HINT_T2);
    }
}

#elif RADIX_SHUFFLE == 1

template<uint16_t sh>
inline void radix_shuffle(uint64_t *counts, index_t *src, index_t *dest, uint64_t size) {
    _mm_prefetch(counts, _MM_HINT_NTA);
    Vec4q vec;
    Vec4q digitVec;
    int64_t values[4];
    int64_t digits[4];
    for (uint64_t x = 0; x < size; x += 4) {
        _mm_prefetch(src + x + 64, _MM_HINT_T0);
        vec.load(src + x);
        digitVec = (vec >> sh) & 0xff;

        vec.store(values);
        digitVec.store(digits);

        dest[counts[digits[0]]] = values[0];
        counts[digits[0]]++;

        dest[counts[digits[1]]] = values[1];
        counts[digits[1]]++;

        dest[counts[digits[2]]] = values[2];
        counts[digits[2]]++;

        dest[counts[digits[3]]] = values[3];
        counts[digits[3]]++;
    }
}

#elif RADIX_SHUFFLE == 2
template<uint16_t sh>
inline void radix_shuffle(uint64_t* counts, int64_t* src, int64_t* dest, uint64_t size) {
    _mm_prefetch(counts, _MM_HINT_NTA);
    Vec8q vec;
    Vec8q digitVec;
    int64_t values[8];
    int64_t digits[8];
    for (uint64_t x = 0; x < size; x += 8) {
        _mm_prefetch(src + x + 64, _MM_HINT_T0);
        vec.load(src + x);
        digitVec = (vec >> sh) & 0xff;

        vec.store(values);
        digitVec.store(digits);

        dest[counts[digits[0]]] = values[0];
        counts[digits[0]]++;

        dest[counts[digits[1]]] = values[1];
        counts[digits[1]]++;

        dest[counts[digits[2]]] = values[2];
        counts[digits[2]]++;

        dest[counts[digits[3]]] = values[3];
        counts[digits[3]]++;

        dest[counts[digits[4]]] = values[4];
        counts[digits[4]]++;

        dest[counts[digits[5]]] = values[5];
        counts[digits[5]]++;

        dest[counts[digits[6]]] = values[6];
        counts[digits[6]]++;

        dest[counts[digits[7]]] = values[7];
        counts[digits[7]]++;
    }
}
#endif

void radix_sort_long_index_asc_in_place(index_t *array, uint64_t size) {
    rscounts_t counts;
    memset(&counts, 0, 256 * 8 * sizeof(uint64_t));
    auto *cpy = (index_t *) malloc(size * sizeof(index_t));
    int64_t o8 = 0, o7 = 0, o6 = 0, o5 = 0, o4 = 0, o3 = 0, o2 = 0, o1 = 0;
    int64_t t8, t7, t6, t5, t4, t3, t2, t1;
    int64_t x;

    // calculate counts
    _mm_prefetch(counts.c8, _MM_HINT_NTA);
    for (x = 0; x < size; x++) {
        t8 = array[x].ts & 0xffu;
        t7 = (array[x].ts >> 8u) & 0xffu;
        t6 = (array[x].ts >> 16u) & 0xffu;
        t5 = (array[x].ts >> 24u) & 0xffu;
        t4 = (array[x].ts >> 32u) & 0xffu;
        t3 = (array[x].ts >> 40u) & 0xffu;
        t2 = (array[x].ts >> 48u) & 0xffu;
        t1 = (array[x].ts >> 56u) & 0xffu;
        counts.c8[t8]++;
        counts.c7[t7]++;
        counts.c6[t6]++;
        counts.c5[t5]++;
        counts.c4[t4]++;
        counts.c3[t3]++;
        counts.c2[t2]++;
        counts.c1[t1]++;
        _mm_prefetch(array + x + 64, _MM_HINT_T2);
    }

    // convert counts to offsets
    _mm_prefetch(&counts, _MM_HINT_NTA);
    for (x = 0; x < 256; x++) {
        t8 = o8 + counts.c8[x];
        t7 = o7 + counts.c7[x];
        t6 = o6 + counts.c6[x];
        t5 = o5 + counts.c5[x];
        t4 = o4 + counts.c4[x];
        t3 = o3 + counts.c3[x];
        t2 = o2 + counts.c2[x];
        t1 = o1 + counts.c1[x];
        counts.c8[x] = o8;
        counts.c7[x] = o7;
        counts.c6[x] = o6;
        counts.c5[x] = o5;
        counts.c4[x] = o4;
        counts.c3[x] = o3;
        counts.c2[x] = o2;
        counts.c1[x] = o1;
        o8 = t8;
        o7 = t7;
        o6 = t6;
        o5 = t5;
        o4 = t4;
        o3 = t3;
        o2 = t2;
        o1 = t1;
    }

    // radix
    radix_shuffle<0u>(counts.c8, array, cpy, size);
    radix_shuffle<8u>(counts.c7, cpy, array, size);
    radix_shuffle<16u>(counts.c6, array, cpy, size);
    radix_shuffle<24u>(counts.c5, cpy, array, size);
    radix_shuffle<32u>(counts.c4, array, cpy, size);
    radix_shuffle<40u>(counts.c3, cpy, array, size);
    radix_shuffle<48u>(counts.c2, array, cpy, size);
    radix_shuffle<56u>(counts.c1, cpy, array, size);
    free(cpy);
}

inline void swap(index_t *a, index_t *b) {
    const auto t = *a;
    *a = *b;
    *b = t;
}

/**
 * This function takes last element as pivot, places
 *  the pivot element at its correct position in sorted
 *   array, and places all smaller (smaller than pivot)
 *  to left of pivot and all greater elements to right
 *  of pivot
 *
 **/
uint64_t partition(index_t *index, uint64_t low, uint64_t high) {
    const auto pivot = index[high].ts;    // pivot
    auto i = (low - 1);  // Index of smaller element

    for (uint64_t j = low; j <= high - 1; j++) {
        // If current element is smaller than or
        // equal to pivot
        if (index[j].ts <= pivot) {
            i++;    // increment index of smaller element
            swap(&index[i], &index[j]);
        }
    }
    swap(&index[i + 1], &index[high]);
    return (i + 1);
}

/**
 * The main function that implements QuickSort
 * arr[] --> Array to be sorted,
 * low  --> Starting index,
 * high  --> Ending index
 **/
void quick_sort_long_index_asc_in_place(index_t *arr, int64_t low, int64_t high) {
    if (low < high) {
        /* pi is partitioning index, arr[p] is now
           at right place */
        uint64_t pi = partition(arr, low, high);

        // Separately sort elements before
        // partition and after partition
        quick_sort_long_index_asc_in_place(arr, low, pi - 1);
        quick_sort_long_index_asc_in_place(arr, pi + 1, high);
    }
}

inline void sort(index_t *index, int64_t size) {
    if (size < 600) {
        quick_sort_long_index_asc_in_place(index, 0, size - 1);
    } else {
        radix_sort_long_index_asc_in_place(index, size);
    }
}

typedef struct {
    uint64_t value;
    uint32_t index_index;
} loser_node_t;

typedef struct {
    index_t *index;
    uint64_t pos;
    uint64_t size;
} index_entry_t;

typedef struct {
    index_t *index;
    int64_t size;
} java_index_entry_t;


template<class T>
inline void re_shuffle_internal(T *src, T *dest, index_t *index, int64_t count) {
    for (int64_t i = 0; i < count; i++) {
        _mm_prefetch(index + 64, _MM_HINT_T0);
        dest[i] = src[index[i].i];
    }
}

template<class T>
inline void re_shuffle(jlong src, jlong dest, jlong index, jlong count) {
    re_shuffle_internal<T>(
            reinterpret_cast<T *>(src),
            reinterpret_cast<T *>(dest),
            reinterpret_cast<index_t *>(index),
            count
    );
}

template<class T>
inline void merge_shuffle_internal(T *src1, T *src2, T *dest, index_t *index, int64_t count) {

    T *sources[] = {src2, src1};
    for (long i = 0; i < count; i++) {
        const auto r = reinterpret_cast<uint64_t>(index[i].i);
        const uint64_t pick = r >> 63u;
        const auto row = r & ~(1LLu << 63u);
        dest[i] = sources[pick][row];
    }
}

template<class T>
inline void merge_shuffle(jlong src1, jlong src2, jlong dest, jlong index, jlong count) {
    merge_shuffle_internal<T>(
            reinterpret_cast<T *>(src1),
            reinterpret_cast<T *>(src2),
            reinterpret_cast<T *>(dest),
            reinterpret_cast<index_t *>(index),
            count
    );
}

template<class T>
inline void merge_shuffle_internal_top(T *src1, T *src2, T *dest, index_t *index, int64_t count, int64_t topOffset) {
    T *sources[] = {src2, src1};
    int64_t sz = sizeof(T);
    int64_t shifts[] = {0, static_cast<int64_t>(topOffset/sz)};
    _mm_prefetch(shifts, _MM_HINT_NTA);
    for (long i = 0; i < count; i++) {
        const auto r = reinterpret_cast<uint64_t>(index[i].i);
        const int64_t pick = r >> 63u;
        const auto row = r & ~(1LLu << 63u);
        dest[i] = sources[pick][row + shifts[pick]];
    }
}

template<class T>
inline void merge_shuffle_top(jlong src1, jlong src2, jlong dest, jlong index, jlong count, jlong top) {
    merge_shuffle_internal_top<T>(
            reinterpret_cast<T *>(src1),
            reinterpret_cast<T *>(src2),
            reinterpret_cast<T *>(dest),
            reinterpret_cast<index_t *>(index),
            count,
            top
    );
}

void k_way_merge_long_index(
        index_entry_t *indexes,
        uint32_t entries_count,
        uint32_t sentinels_at_start,
        index_t *dest
) {

    // calculate size of the tree
    uint32_t tree_size = entries_count * 2;
    uint64_t merged_index_pos = 0;
    uint32_t sentinels_left = entries_count - sentinels_at_start;

    loser_node_t tree[tree_size];

    // seed the bottom of the tree with index values
    for (uint32_t i = 0; i < entries_count; i++) {
        if (indexes[i].index != nullptr) {
            tree[entries_count + i].value = indexes[i].index->ts;
        } else {
            tree[entries_count + i].value = L_MAX;
        }
        tree[entries_count + i].index_index = entries_count + i;
    }

    // seed the entire tree from bottom up
    for (uint32_t i = tree_size - 1; i > 1; i -= 2) {
        uint32_t winner;
        if (tree[i].value < tree[i - 1].value) {
            winner = i;
        } else {
            winner = i - 1;
        }
        tree[i / 2] = tree[winner];
    }

    // take the first winner
    auto winner_index = tree[1].index_index;
    index_entry_t *winner = indexes + winner_index - entries_count;
    if (winner->pos < winner->size) {
        dest[merged_index_pos++] = winner->index[winner->pos];
    } else {
        sentinels_left--;
    }


    // full run
    while (sentinels_left > 0) {

        // back fill the winning index
        if (PREDICT_TRUE(++winner->pos < winner->size)) {
            tree[winner_index].value = winner->index[winner->pos].ts;
        } else {
            tree[winner_index].value = L_MAX;
            sentinels_left--;
        }

        if (sentinels_left == 0) {
            break;
        }

        _mm_prefetch(tree, _MM_HINT_NTA);
        while (PREDICT_TRUE(winner_index > 1)) {
            const auto right_child = winner_index % 2 == 1 ? winner_index - 1 : winner_index + 1;
            const auto target = winner_index / 2;
            if (tree[winner_index].value < tree[right_child].value) {
                tree[target] = tree[winner_index];
            } else {
                tree[target] = tree[right_child];
            }
            winner_index = target;
        }
        winner_index = tree[1].index_index;
        winner = indexes + winner_index - entries_count;
        _mm_prefetch(winner, _MM_HINT_NTA);
        dest[merged_index_pos++] = winner->index[winner->pos];
    }
}

template<class T>
inline int64_t binary_search(T *data, int64_t value, int64_t low, int64_t high, int32_t scan_dir) {
    while (low < high) {
        int64_t mid = (low + high) / 2;
        T midVal = data[mid];

        if (midVal < value) {
            if (low < mid) {
                low = mid;
            } else {
                if (data[high] > value) {
                    return low;
                }
                return high;
            }
        } else if (midVal > value)
            high = mid;
        else {
            // In case of multiple equal values, find the first
            mid += scan_dir;
            while (mid > 0 && mid <= high && data[mid] == midVal) {
                mid += scan_dir;
            }
            return mid - scan_dir;
        }
    }

    if (data[low] > value) {
        return low - 1;
    }
    return low;
}

inline void make_timestamp_index(const int64_t *data, int64_t low, int64_t high, index_t *dest) {
    for (int64_t l = low; l <= high; l++) {
        dest[l - low].ts = data[l];
        dest[l - low].i = l | (1ull << 63);
    }
}

template<class T>
inline void merge_copy_var_column(
        index_t *merge_index,
        int64_t merge_index_size,
        int64_t *src_data_fix,
        char *src_data_var,
        int64_t *src_ooo_fix,
        char *src_ooo_var,
        int64_t *dst_fix,
        char *dst_var,
        int64_t dst_var_offset,
        T mult
) {
    int64_t *src_fix[] = {src_ooo_fix, src_data_fix};
    char *src_var[] = {src_ooo_var, src_data_var};

    for (int64_t l = 0; l < merge_index_size; l++) {
        _mm_prefetch(merge_index + 64, _MM_HINT_T0);
        dst_fix[l] = dst_var_offset;
        const uint64_t row = merge_index[l].i;
        const uint32_t bit = (row >> 63);
        const uint64_t rr = row & ~(1ull << 63);
        const int64_t offset = src_fix[bit][rr];
        char *src_var_ptr = src_var[bit] + offset;
        auto len = *reinterpret_cast<T *>(src_var_ptr);
        auto char_count = len > 0 ? len * mult : 0;
        reinterpret_cast<T *>(dst_var + dst_var_offset)[0] = len;
        A_memcpy(dst_var + dst_var_offset + sizeof(T), src_var_ptr + sizeof(T), char_count);
        dst_var_offset += char_count + sizeof(T);
    }
}

template<class T>
inline void merge_copy_var_column_top(
        index_t *merge_index,
        int64_t merge_index_size,
        int64_t *src_data_fix,
        int64_t src_data_fix_offset,
        char *src_data_var,
        int64_t *src_ooo_fix,
        char *src_ooo_var,
        int64_t *dst_fix,
        char *dst_var,
        int64_t dst_var_offset,
        T mult
) {
    int64_t *src_fix[] = {src_ooo_fix, src_data_fix};
    char *src_var[] = {src_ooo_var, src_data_var};
    int64_t fix_shifts[] = {0, src_data_fix_offset / 8};

    for (int64_t l = 0; l < merge_index_size; l++) {
        _mm_prefetch(merge_index + 64, _MM_HINT_T0);
        dst_fix[l] = dst_var_offset;
        const uint64_t row = merge_index[l].i;
        const uint32_t bit = (row >> 63);
        const uint64_t rr = row & ~(1ull << 63);
        const int64_t offset = src_fix[bit][rr + fix_shifts[bit]];
        char *src_var_ptr = src_var[bit] + offset;
        auto len = *reinterpret_cast<T *>(src_var_ptr);
        auto char_count = len > 0 ? len * mult : 0;
        reinterpret_cast<T *>(dst_var + dst_var_offset)[0] = len;
        A_memcpy(dst_var + dst_var_offset + sizeof(T), src_var_ptr + sizeof(T), char_count);
        dst_var_offset += char_count + sizeof(T);
    }
}

template<class T>
inline void set_memory_vanilla(T *addr, T value, int64_t count) {
    for (int64_t i = 0; i < count; i++) {
        addr[i] = value;
    }
}

template<class T>
inline void set_var_refs(int64_t *addr, int64_t offset, int64_t count) {
    auto inc = sizeof(T);
    for (int64_t i = 0; i < count; i++) {
        addr[i] = offset + i * inc;
    }
}

inline void copy_index(index_t* index, int64_t index_size, int64_t* dest) {
    for (int64_t i = 0; i < index_size; i++) {
        dest[i] = index[i].ts;
    }
}

extern "C" {

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_oooMergeCopyStrColumn(JNIEnv *env, jclass cl,
                                               jlong merge_index,
                                               jlong merge_index_size,
                                               jlong src_data_fix,
                                               jlong src_data_var,
                                               jlong src_ooo_fix,
                                               jlong src_ooo_var,
                                               jlong dst_fix,
                                               jlong dst_var,
                                               jlong dst_var_offset) {
    merge_copy_var_column<int32_t>(
            reinterpret_cast<index_t *>(merge_index),
            reinterpret_cast<int64_t>(merge_index_size),
            reinterpret_cast<int64_t *>(src_data_fix),
            reinterpret_cast<char *>(src_data_var),
            reinterpret_cast<int64_t *>(src_ooo_fix),
            reinterpret_cast<char *>(src_ooo_var),
            reinterpret_cast<int64_t *>(dst_fix),
            reinterpret_cast<char *>(dst_var),
            reinterpret_cast<int64_t>(dst_var_offset),
            2
    );
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_oooMergeCopyStrColumnWithTop(JNIEnv *env, jclass cl,
                                                      jlong merge_index,
                                                      jlong merge_index_size,
                                                      jlong src_data_fix,
                                                      jlong src_data_fix_offset,
                                                      jlong src_data_var,
                                                      jlong src_ooo_fix,
                                                      jlong src_ooo_var,
                                                      jlong dst_fix,
                                                      jlong dst_var,
                                                      jlong dst_var_offset) {
    merge_copy_var_column_top<int32_t>(
            reinterpret_cast<index_t *>(merge_index),
            reinterpret_cast<int64_t>(merge_index_size),
            reinterpret_cast<int64_t *>(src_data_fix),
            src_data_fix_offset,
            reinterpret_cast<char *>(src_data_var),
            reinterpret_cast<int64_t *>(src_ooo_fix),
            reinterpret_cast<char *>(src_ooo_var),
            reinterpret_cast<int64_t *>(dst_fix),
            reinterpret_cast<char *>(dst_var),
            reinterpret_cast<int64_t>(dst_var_offset),
            2);
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_oooMergeCopyBinColumnWithTop(JNIEnv *env, jclass cl,
                                                      jlong merge_index,
                                                      jlong merge_index_size,
                                                      jlong src_data_fix,
                                                      jlong src_data_fix_offset,
                                                      jlong src_data_var,
                                                      jlong src_ooo_fix,
                                                      jlong src_ooo_var,
                                                      jlong dst_fix,
                                                      jlong dst_var,
                                                      jlong dst_var_offset) {
    merge_copy_var_column_top<int64_t>(
            reinterpret_cast<index_t *>(merge_index),
            reinterpret_cast<int64_t>(merge_index_size),
            reinterpret_cast<int64_t *>(src_data_fix),
            src_data_fix_offset,
            reinterpret_cast<char *>(src_data_var),
            reinterpret_cast<int64_t *>(src_ooo_fix),
            reinterpret_cast<char *>(src_ooo_var),
            reinterpret_cast<int64_t *>(dst_fix),
            reinterpret_cast<char *>(dst_var),
            reinterpret_cast<int64_t>(dst_var_offset),
            1);
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_oooMergeCopyBinColumn(JNIEnv *env, jclass cl,
                                               jlong merge_index,
                                               jlong merge_index_size,
                                               jlong src_data_fix,
                                               jlong src_data_var,
                                               jlong src_ooo_fix,
                                               jlong src_ooo_var,
                                               jlong dst_fix,
                                               jlong dst_var,
                                               jlong dst_var_offset) {
    merge_copy_var_column<int64_t>(
            reinterpret_cast<index_t *>(merge_index),
            reinterpret_cast<int64_t>(merge_index_size),
            reinterpret_cast<int64_t *>(src_data_fix),
            reinterpret_cast<char *>(src_data_var),
            reinterpret_cast<int64_t *>(src_ooo_fix),
            reinterpret_cast<char *>(src_ooo_var),
            reinterpret_cast<int64_t *>(dst_fix),
            reinterpret_cast<char *>(dst_var),
            reinterpret_cast<int64_t>(dst_var_offset),
            1
    );
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_sortLongIndexAscInPlace(JNIEnv *env, jclass cl, jlong pLong, jlong len) {
    sort(reinterpret_cast<index_t *>(pLong), len);
}

JNIEXPORT jlong JNICALL
Java_io_questdb_std_Vect_mergeLongIndexesAsc(JNIEnv *env, jclass cl, jlong pIndexStructArray, jint count) {
    // prepare merge entries
    // they need to have mutable current position "pos" in index

    if (count < 1) {
        return 0;
    }

    const java_index_entry_t *java_entries = reinterpret_cast<java_index_entry_t *>(pIndexStructArray);
    if (count == 1) {
        return reinterpret_cast<jlong>(java_entries[0].index);
    }

    uint32_t size = ceil_pow_2(count);
    index_entry_t entries[size];
    uint64_t merged_index_size = 0;
    for (jint i = 0; i < count; i++) {
        entries[i].index = java_entries[i].index;
        entries[i].pos = 0;
        entries[i].size = java_entries[i].size;
        merged_index_size += java_entries[i].size;
    }

    if (count < size) {
        for (uint32_t i = count; i < size; i++) {
            entries[i].index = nullptr;
            entries[i].pos = 0;
            entries[i].size = -1;
        }
    }

    auto *merged_index = reinterpret_cast<index_t *>(malloc(merged_index_size * sizeof(index_t)));
    k_way_merge_long_index(entries, size, size - count, merged_index);
    return reinterpret_cast<jlong>(merged_index);
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_freeMergedIndex(JNIEnv *env, jclass cl, jlong pIndex) {
    free(reinterpret_cast<void *>(pIndex));
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_indexReshuffle32Bit(JNIEnv *env, jclass cl, jlong pSrc, jlong pDest, jlong pIndex,
                                             jlong count) {
    re_shuffle<uint32_t>(pSrc, pDest, pIndex, count);
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_indexReshuffle64Bit(JNIEnv *env, jclass cl, jlong pSrc, jlong pDest, jlong pIndex,
                                             jlong count) {
    re_shuffle<uint64_t>(pSrc, pDest, pIndex, count);
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_indexReshuffle16Bit(JNIEnv *env, jclass cl, jlong pSrc, jlong pDest, jlong pIndex,
                                             jlong count) {
    re_shuffle<uint16_t>(pSrc, pDest, pIndex, count);
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_indexReshuffle8Bit(JNIEnv *env, jclass cl, jlong pSrc, jlong pDest, jlong pIndex,
                                            jlong count) {
    re_shuffle<uint8_t>(pSrc, pDest, pIndex, count);
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_mergeShuffle8Bit(JNIEnv *env, jclass cl, jlong src1, jlong src2, jlong dest, jlong index,
                                          jlong count) {
    merge_shuffle<int8_t>(src1, src2, dest, index, count);
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_mergeShuffle16Bit(JNIEnv *env, jclass cl, jlong src1, jlong src2, jlong dest, jlong index,
                                           jlong count) {
    merge_shuffle<int16_t>(src1, src2, dest, index, count);
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_mergeShuffle32Bit(JNIEnv *env, jclass cl, jlong src1, jlong src2, jlong dest, jlong index,
                                           jlong count) {
    merge_shuffle<int32_t>(src1, src2, dest, index, count);
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_mergeShuffle64Bit(JNIEnv *env, jclass cl, jlong src1, jlong src2, jlong dest, jlong index,
                                           jlong count) {
    merge_shuffle<int64_t>(src1, src2, dest, index, count);
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_mergeShuffleWithTop64Bit(JNIEnv *env, jclass cl, jlong src1, jlong src2, jlong dest,
                                                  jlong index,
                                                  jlong count, jlong topOffset) {
    merge_shuffle_top<int64_t>(src1, src2, dest, index, count, topOffset);
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_mergeShuffleWithTop32Bit(JNIEnv *env, jclass cl, jlong src1, jlong src2, jlong dest,
                                                  jlong index,
                                                  jlong count, jlong topOffset) {
    merge_shuffle_top<int32_t>(src1, src2, dest, index, count, topOffset);
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_mergeShuffleWithTop16Bit(JNIEnv *env, jclass cl, jlong src1, jlong src2, jlong dest,
                                                  jlong index,
                                                  jlong count, jlong topOffset) {
    merge_shuffle_top<int16_t>(src1, src2, dest, index, count, topOffset);
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_mergeShuffleWithTop8Bit(JNIEnv *env, jclass cl, jlong src1, jlong src2, jlong dest,
                                                 jlong index,
                                                 jlong count, jlong topOffset) {
    merge_shuffle_top<int8_t>(src1, src2, dest, index, count, topOffset);
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_flattenIndex(JNIEnv *env, jclass cl, jlong pIndex,
                                      jlong count) {
    auto *index = reinterpret_cast<index_t *>(pIndex);
    for (int64_t i = 0; i < count; i++) {
        index[i].i = i;
    }
}

JNIEXPORT jlong JNICALL
Java_io_questdb_std_Vect_binarySearch64Bit(JNIEnv *env, jclass cl, jlong pData, jlong value, jlong low,
                                           jlong high, jint scan_dir) {
    return binary_search<int64_t>(reinterpret_cast<int64_t *>(pData), value, low, high, scan_dir);
}

JNIEXPORT jlong JNICALL
Java_io_questdb_std_Vect_binarySearchIndexT(JNIEnv *env, jclass cl, jlong pData, jlong value, jlong low,
                                            jlong high, jint scan_dir) {
    return binary_search<index_t>(reinterpret_cast<index_t *>(pData), value, low, high, scan_dir);
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_makeTimestampIndex(JNIEnv *env, jclass cl, jlong pData, jlong low,
                                            jlong high, jlong pIndex) {
    make_timestamp_index(
            reinterpret_cast<int64_t *>(pData),
            low,
            high,
            reinterpret_cast<index_t *>(pIndex)
    );
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_setMemoryLong(JNIEnv *env, jclass cl, jlong pData, jlong value,
                                       jlong count) {
    set_memory_vanilla<int64_t>(
            reinterpret_cast<int64_t *>(pData),
            reinterpret_cast<int64_t>(value),
            (int64_t) (count)
    );
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_setMemoryInt(JNIEnv *env, jclass cl, jlong pData, jint value,
                                      jlong count) {
    set_memory_vanilla<jint>(
            reinterpret_cast<jint *>(pData),
            value,
            (int64_t) (count)
    );
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_setMemoryDouble(JNIEnv *env, jclass cl, jlong pData, jdouble value,
                                         jlong count) {
    set_memory_vanilla<jdouble>(
            reinterpret_cast<jdouble *>(pData),
            value,
            (int64_t) (count)
    );
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_setMemoryFloat(JNIEnv *env, jclass cl, jlong pData, jfloat value,
                                        jlong count) {
    set_memory_vanilla<jfloat>(
            reinterpret_cast<jfloat *>(pData),
            value,
            (int64_t) (count)
    );
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_setMemoryShort(JNIEnv *env, jclass cl, jlong pData, jshort value,
                                        jlong count) {
    set_memory_vanilla<jshort>(
            reinterpret_cast<jshort *>(pData),
            value,
            (int64_t) (count)
    );
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_setVarColumnRefs32Bit(JNIEnv *env, jclass cl, jlong pData, jlong offset,
                                               jlong count) {
    set_var_refs<int32_t>(
            reinterpret_cast<int64_t *>(pData),
            reinterpret_cast<int64_t>(offset),
            reinterpret_cast<int64_t>(count)
    );
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_setVarColumnRefs64Bit(JNIEnv *env, jclass cl, jlong pData, jlong offset,
                                               jlong count) {
    set_var_refs<int64_t>(
            reinterpret_cast<int64_t *>(pData),
            reinterpret_cast<int64_t>(offset),
            reinterpret_cast<int64_t>(count)
    );
}

JNIEXPORT void JNICALL
Java_io_questdb_std_Vect_oooCopyIndex(JNIEnv *env, jclass cl, jlong pIndex, jlong index_size,
                                               jlong pDest) {
    copy_index(
            reinterpret_cast<index_t *>(pIndex),
            reinterpret_cast<int64_t>(index_size),
            reinterpret_cast<int64_t*>(pDest)
    );
}
}

