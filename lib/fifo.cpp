// SPDX-License-Identifier: GPL-2.0-only WITH linking exception
/*
 *	A simple fifo implements using single linked list. The core will be 
 *	maintain outside the fifo, but the fifo will give black a node size
 *	memory every time you call deque, unless the fifo is empty.
 *
 */
#include <cstring>
#include <stdexcept>
#include <gromox/fifo.hpp>
#include <gromox/util.hpp>

/*
 *	create an allocator for fifo
 *	@param
 *		data_size	 size of item
 *		max_size	 quantity of itmes
 *		threads_safe need to be created as thread_safe
 *	@return
 *		pointer to lib buffer object
 */
LIB_BUFFER* fifo_allocator_init(size_t data_size, size_t max_size, BOOL thread_safe)
{
	return lib_buffer_init(data_size+EXTRA_FIFOITEM_SIZE, max_size, 
						   thread_safe);
}

/*
 *	destroy the allocator
 *	@param
 *		pallocator	  pointer to the allocator object
 */												
void fifo_allocator_free(LIB_BUFFER* pallocator)
{
	lib_buffer_free(pallocator);
}

/*
 *	init a fifo with the specified size of data and max capacity.
 *
 *	@param
 *		pfifo		[in]	the fifo that will be init
 *		pbuf_pool	[in]	the outside allocator that manage the 
 *							memory core
 *		data_size			the size of the data in every fifo node
 *		max_size			the max capacity of the fifo
 */
FIFO::FIFO(LIB_BUFFER *pbuf_pool, size_t ds, size_t ms) :
	mbuf_pool(pbuf_pool), data_size(ds), max_size(ms)
{
	if (pbuf_pool == nullptr)
		throw std::invalid_argument("FIFO with no LIB_BUFFER");
	single_list_init(&mlist);
}

/*
 *	enqueue the data into the specified fifo
 *
 *	@param
 *		pfifo [in]	   the fifo that will 
 *					   enqueue the data onto
 *		pdata  [in]	   pointer to the data
 *					   that will be enqueued
 *	@return
 *		TRUE		success
 *		FALSE		the fifo is full
 */
BOOL FIFO::enqueue(void *pdata)
{
	auto pfifo = this;
#ifdef _DEBUG_UMTA
	if (pdata == nullptr)
		debug_info("[fifo]: fifo_enqueue, param NULL");
#endif
	
	if (pfifo->cur_size >= pfifo->max_size) {
		return FALSE;
	}
	auto node = static_cast<SINGLE_LIST_NODE *>(lib_buffer_get(pfifo->mbuf_pool));
	node->pdata = (char*)node + sizeof(SINGLE_LIST_NODE);
	memcpy(node->pdata, pdata, pfifo->data_size);

	single_list_append_as_tail(&pfifo->mlist, node);
	pfifo->cur_size += 1;
	return TRUE;
}

/*
 *	dequeue the top item from the specified fifo, and give
 *	back the data size of memory to the outside allocator
 *	
 *	@param	
 *		pfifo [in]	   the specified fifo object
 */
void FIFO::dequeue()
{
	auto pfifo = this;
	SINGLE_LIST_NODE   *node = NULL;
	if (pfifo->cur_size <= 0) {
		return;
	}
	node = single_list_pop_front(&pfifo->mlist);
	lib_buffer_put(pfifo->mbuf_pool, node);
	pfifo->cur_size -= 1;
	return;
}

/*
 *	return a pointer that point to the data at the
 *	front of the specified fifo.
 *
 *	@param
 *		pfifo [in]	   the fifo to get the data from
 *
 *	@return
 *		the pointer that pointer to the data at the 
 *		front of the fifo
 *		NULL if the pfifo is NULL or the fifo is empty
 */
void *FIFO::get_front()
{
	auto pfifo = this;
	SINGLE_LIST_NODE   *node = NULL;
	if (pfifo->cur_size <= 0) {
		return NULL;
	}

	node = single_list_get_head(&pfifo->mlist);
	return node != nullptr ? node->pdata : nullptr;
}
