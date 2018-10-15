#include "common.h"

#include "command.h"

#include "kernel/vc4_packet.h"
#include "../brcm/cle/v3d_decoder.h"
#include "../brcm/clif/clif_dump.h"

/*
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#commandbuffers-pools
 * Command pools are opaque objects that command buffer memory is allocated from, and which allow the implementation to amortize the
 * cost of resource creation across multiple command buffers. Command pools are externally synchronized, meaning that a command pool must
 * not be used concurrently in multiple threads. That includes use via recording commands on any command buffers allocated from the pool,
 * as well as operations that allocate, free, and reset command buffers or the pool itself.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(
		VkDevice                                    device,
		const VkCommandPoolCreateInfo*              pCreateInfo,
		const VkAllocationCallbacks*                pAllocator,
		VkCommandPool*                              pCommandPool)
{
	assert(device);
	assert(pCreateInfo);

	//TODO: allocator is ignored for now
	assert(pAllocator == 0);

	//VK_COMMAND_POOL_CREATE_TRANSIENT_BIT
	//specifies that command buffers allocated from the pool will be short-lived, meaning that they will be reset or freed in a relatively short timeframe.
	//This flag may be used by the implementation to control memory allocation behavior within the pool.
	//--> definitely use pool allocator

	//VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
	//allows any command buffer allocated from a pool to be individually reset to the initial state; either by calling vkResetCommandBuffer, or via the implicit reset when calling vkBeginCommandBuffer.
	//If this flag is not set on a pool, then vkResetCommandBuffer must not be called for any command buffer allocated from that pool.

	//TODO pool family ignored for now

	_commandPool* cp = malloc(sizeof(_commandPool));

	if(!cp)
	{
		return VK_ERROR_OUT_OF_HOST_MEMORY;
	}

	cp->queueFamilyIndex = pCreateInfo->queueFamilyIndex;

	//initial number of command buffers to hold
	int numCommandBufs = 100;
	int controlListSize = ARM_PAGE_SIZE * 100;

	//if(pCreateInfo->flags & VK_COMMAND_POOL_CREATE_TRANSIENT_BIT)
	{
		//use pool allocator
		void* pamem = malloc(numCommandBufs * sizeof(_commandBuffer));
		if(!pamem)
		{
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		cp->pa = createPoolAllocator(pamem, sizeof(_commandBuffer), numCommandBufs * sizeof(_commandBuffer));

		void* cpamem = malloc(controlListSize);
		if(!cpamem)
		{
			return VK_ERROR_OUT_OF_HOST_MEMORY;
		}
		cp->cpa = createConsecutivePoolAllocator(cpamem, ARM_PAGE_SIZE, controlListSize);
	}

	*pCommandPool = (VkCommandPool)cp;

	return VK_SUCCESS;
}

/*
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#commandbuffer-allocation
 * vkAllocateCommandBuffers can be used to create multiple command buffers. If the creation of any of those command buffers fails,
 * the implementation must destroy all successfully created command buffer objects from this command, set all entries of the pCommandBuffers array to NULL and return the error.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(
		VkDevice                                    device,
		const VkCommandBufferAllocateInfo*          pAllocateInfo,
		VkCommandBuffer*                            pCommandBuffers)
{
	assert(device);
	assert(pAllocateInfo);
	assert(pCommandBuffers);

	VkResult res = VK_SUCCESS;

	_commandPool* cp = (_commandPool*)pAllocateInfo->commandPool;

	//if(cp->usePoolAllocator)
	{
		for(int c = 0; c < pAllocateInfo->commandBufferCount; ++c)
		{
			pCommandBuffers[c] = poolAllocate(&cp->pa);

			if(!pCommandBuffers[c])
			{
				res = VK_ERROR_OUT_OF_HOST_MEMORY;
				break;
			}

			pCommandBuffers[c]->shaderRecCount = 0;
			pCommandBuffers[c]->usageFlags = 0;
			pCommandBuffers[c]->state = CMDBUF_STATE_INITIAL;
			pCommandBuffers[c]->cp = cp;
			clInit(&pCommandBuffers[c]->binCl, consecutivePoolAllocate(&cp->cpa, 1));
			clInit(&pCommandBuffers[c]->handlesCl, consecutivePoolAllocate(&cp->cpa, 1));
			clInit(&pCommandBuffers[c]->shaderRecCl, consecutivePoolAllocate(&cp->cpa, 1));
			clInit(&pCommandBuffers[c]->uniformsCl, consecutivePoolAllocate(&cp->cpa, 1));

			if(!pCommandBuffers[c]->binCl.buffer)
			{
				res = VK_ERROR_OUT_OF_HOST_MEMORY;
				break;
			}

			if(!pCommandBuffers[c]->handlesCl.buffer)
			{
				res = VK_ERROR_OUT_OF_HOST_MEMORY;
				break;
			}

			if(!pCommandBuffers[c]->shaderRecCl.buffer)
			{
				res = VK_ERROR_OUT_OF_HOST_MEMORY;
				break;
			}

			if(!pCommandBuffers[c]->uniformsCl.buffer)
			{
				res = VK_ERROR_OUT_OF_HOST_MEMORY;
				break;
			}
		}
	}

	if(res != VK_SUCCESS)
	{
		//if(cp->usePoolAllocator)
		{
			for(int c = 0; c < pAllocateInfo->commandBufferCount; ++c)
			{
				consecutivePoolFree(&cp->cpa, &pCommandBuffers[c]->binCl, pCommandBuffers[c]->binCl.numBlocks);
				consecutivePoolFree(&cp->cpa, &pCommandBuffers[c]->handlesCl, pCommandBuffers[c]->binCl.numBlocks);
				consecutivePoolFree(&cp->cpa, &pCommandBuffers[c]->shaderRecCl, pCommandBuffers[c]->binCl.numBlocks);
				consecutivePoolFree(&cp->cpa, &pCommandBuffers[c]->uniformsCl, pCommandBuffers[c]->binCl.numBlocks);
				poolFree(&cp->pa, pCommandBuffers[c]);
				pCommandBuffers[c] = 0;
			}
		}
	}

	return res;
}

/*
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#vkBeginCommandBuffer
 */
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(
		VkCommandBuffer                             commandBuffer,
		const VkCommandBufferBeginInfo*             pBeginInfo)
{
	assert(commandBuffer);
	assert(pBeginInfo);

	//TODO

	//VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	//specifies that each recording of the command buffer will only be submitted once, and the command buffer will be reset and recorded again between each submission.

	//VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT
	//specifies that a secondary command buffer is considered to be entirely inside a render pass. If this is a primary command buffer, then this bit is ignored

	//VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT
	//specifies that a command buffer can be resubmitted to a queue while it is in the pending state, and recorded into multiple primary command buffers

	//When a command buffer begins recording, all state in that command buffer is undefined

	struct drm_vc4_submit_cl submitCl =
	{
		.color_read.hindex = ~0,
		.zs_read.hindex = ~0,
		.color_write.hindex = ~0,
		.msaa_color_write.hindex = ~0,
		.zs_write.hindex = ~0,
		.msaa_zs_write.hindex = ~0,
	};

	commandBuffer->usageFlags = pBeginInfo->flags;
	commandBuffer->shaderRecCount = 0;
	commandBuffer->state = CMDBUF_STATE_RECORDING;
	commandBuffer->submitCl = submitCl;


	return VK_SUCCESS;
}

/*
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#vkEndCommandBuffer
 * If there was an error during recording, the application will be notified by an unsuccessful return code returned by vkEndCommandBuffer.
 * If the application wishes to further use the command buffer, the command buffer must be reset. The command buffer must have been in the recording state,
 * and is moved to the executable state.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(
		VkCommandBuffer                             commandBuffer)
{
	assert(commandBuffer);

	//Increment the semaphore indicating that binning is done and
	//unblocking the render thread.  Note that this doesn't act
	//until the FLUSH completes.
	//The FLUSH caps all of our bin lists with a
	//VC4_PACKET_RETURN.
	clFit(commandBuffer, &commandBuffer->binCl, V3D21_INCREMENT_SEMAPHORE_length);
	clInsertIncrementSemaphore(&commandBuffer->binCl);
	clFit(commandBuffer, &commandBuffer->binCl, V3D21_FLUSH_length);
	clInsertFlush(&commandBuffer->binCl);

	commandBuffer->state = CMDBUF_STATE_EXECUTABLE;

	return VK_SUCCESS;
}

/*
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#vkQueueSubmit
 * vkQueueSubmit is a queue submission command, with each batch defined by an element of pSubmits as an instance of the VkSubmitInfo structure.
 * Batches begin execution in the order they appear in pSubmits, but may complete out of order.
 * Fence and semaphore operations submitted with vkQueueSubmit have additional ordering constraints compared to other submission commands,
 * with dependencies involving previous and subsequent queue operations. Information about these additional constraints can be found in the semaphore and
 * fence sections of the synchronization chapter.
 * Details on the interaction of pWaitDstStageMask with synchronization are described in the semaphore wait operation section of the synchronization chapter.
 * The order that batches appear in pSubmits is used to determine submission order, and thus all the implicit ordering guarantees that respect it.
 * Other than these implicit ordering guarantees and any explicit synchronization primitives, these batches may overlap or otherwise execute out of order.
 * If any command buffer submitted to this queue is in the executable state, it is moved to the pending state. Once execution of all submissions of a command buffer complete,
 * it moves from the pending state, back to the executable state. If a command buffer was recorded with the VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT flag,
 * it instead moves back to the invalid state.
 * If vkQueueSubmit fails, it may return VK_ERROR_OUT_OF_HOST_MEMORY or VK_ERROR_OUT_OF_DEVICE_MEMORY.
 * If it does, the implementation must ensure that the state and contents of any resources or synchronization primitives referenced by the submitted command buffers and any semaphores
 * referenced by pSubmits is unaffected by the call or its failure. If vkQueueSubmit fails in such a way that the implementation is unable to make that guarantee,
 * the implementation must return VK_ERROR_DEVICE_LOST. See Lost Device.
 */
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(
		VkQueue                                     queue,
		uint32_t                                    submitCount,
		const VkSubmitInfo*                         pSubmits,
		VkFence                                     fence)
{
	assert(queue);

	for(int c = 0; c < pSubmits->waitSemaphoreCount; ++c)
	{
		sem_wait((sem_t*)pSubmits->pWaitSemaphores[c]);
	}

	//TODO: deal with pSubmits->pWaitDstStageMask

	//TODO wait for fence??

	for(int c = 0; c < pSubmits->commandBufferCount; ++c)
	{
		if(pSubmits->pCommandBuffers[c]->state == CMDBUF_STATE_EXECUTABLE)
		{
			pSubmits->pCommandBuffers[c]->state = CMDBUF_STATE_PENDING;
		}
	}

	for(int c = 0; c < pSubmits->commandBufferCount; ++c)
	{
		VkCommandBuffer cmdbuf = pSubmits->pCommandBuffers[c];

		cmdbuf->submitCl.bo_handles = cmdbuf->handlesCl.buffer;
		cmdbuf->submitCl.bo_handle_count = clSize(&cmdbuf->handlesCl) / 4;
		cmdbuf->submitCl.bin_cl = cmdbuf->binCl.buffer;
		cmdbuf->submitCl.bin_cl_size = clSize(&cmdbuf->binCl);
		cmdbuf->submitCl.shader_rec = cmdbuf->shaderRecCl.buffer;
		cmdbuf->submitCl.shader_rec_size = clSize(&cmdbuf->shaderRecCl);
		cmdbuf->submitCl.shader_rec_count = cmdbuf->shaderRecCount;
		cmdbuf->submitCl.uniforms = cmdbuf->uniformsCl.buffer;
		cmdbuf->submitCl.uniforms_size = clSize(&cmdbuf->uniformsCl);

		printf("BCL:\n");
		clDump(cmdbuf->submitCl.bin_cl, cmdbuf->submitCl.bin_cl_size);
		printf("BO handles: ");
		for(int d = 0; d < cmdbuf->submitCl.bo_handle_count; ++d)
		{
			printf("%u ", *((uint32_t*)(cmdbuf->submitCl.bo_handles)+d));
		}
		printf("\nwidth height: %u, %u\n", cmdbuf->submitCl.width, cmdbuf->submitCl.height);
		printf("tile min/max: %u,%u %u,%u\n", cmdbuf->submitCl.min_x_tile, cmdbuf->submitCl.min_y_tile, cmdbuf->submitCl.max_x_tile, cmdbuf->submitCl.max_y_tile);
		printf("color read surf: hindex, offset, bits, flags %u %u %u %u\n", cmdbuf->submitCl.color_read.hindex, cmdbuf->submitCl.color_read.offset, cmdbuf->submitCl.color_read.bits, cmdbuf->submitCl.color_read.flags);
		printf("color write surf: hindex, offset, bits, flags %u %u %u %u\n", cmdbuf->submitCl.color_write.hindex, cmdbuf->submitCl.color_write.offset, cmdbuf->submitCl.color_write.bits, cmdbuf->submitCl.color_write.flags);
		printf("zs read surf: hindex, offset, bits, flags %u %u %u %u\n", cmdbuf->submitCl.zs_read.hindex, cmdbuf->submitCl.zs_read.offset, cmdbuf->submitCl.zs_read.bits, cmdbuf->submitCl.zs_read.flags);
		printf("zs write surf: hindex, offset, bits, flags %u %u %u %u\n", cmdbuf->submitCl.zs_write.hindex, cmdbuf->submitCl.zs_write.offset, cmdbuf->submitCl.zs_write.bits, cmdbuf->submitCl.zs_write.flags);
		printf("msaa color write surf: hindex, offset, bits, flags %u %u %u %u\n", cmdbuf->submitCl.msaa_color_write.hindex, cmdbuf->submitCl.msaa_color_write.offset, cmdbuf->submitCl.msaa_color_write.bits, cmdbuf->submitCl.msaa_color_write.flags);
		printf("msaa zs write surf: hindex, offset, bits, flags %u %u %u %u\n", cmdbuf->submitCl.msaa_zs_write.hindex, cmdbuf->submitCl.msaa_zs_write.offset, cmdbuf->submitCl.msaa_zs_write.bits, cmdbuf->submitCl.msaa_zs_write.flags);
		printf("clear color packed rgba %u %u\n", cmdbuf->submitCl.clear_color[0], cmdbuf->submitCl.clear_color[1]);
		printf("clear z %u\n", cmdbuf->submitCl.clear_z);
		printf("clear s %u\n", cmdbuf->submitCl.clear_s);
		printf("flags %u\n", cmdbuf->submitCl.flags);


		//submit ioctl
		static uint64_t lastFinishedSeqno = 0;
		vc4_cl_submit(controlFd, &cmdbuf->submitCl, &queue->lastEmitSeqno, &lastFinishedSeqno);
	}

	for(int c = 0; c < pSubmits->commandBufferCount; ++c)
	{
		if(pSubmits->pCommandBuffers[c]->state == CMDBUF_STATE_PENDING)
		{
			if(pSubmits->pCommandBuffers[c]->usageFlags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT)
			{
				pSubmits->pCommandBuffers[c]->state = CMDBUF_STATE_INVALID;
			}
			else
			{
				pSubmits->pCommandBuffers[c]->state = CMDBUF_STATE_EXECUTABLE;
			}
		}
	}

	for(int c = 0; c < pSubmits->signalSemaphoreCount; ++c)
	{
		sem_post((sem_t*)pSubmits->pSignalSemaphores[c]);
	}

	return VK_SUCCESS;
}

/*
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#vkFreeCommandBuffers
 * Any primary command buffer that is in the recording or executable state and has any element of pCommandBuffers recorded into it, becomes invalid.
 */
VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(
		VkDevice                                    device,
		VkCommandPool                               commandPool,
		uint32_t                                    commandBufferCount,
		const VkCommandBuffer*                      pCommandBuffers)
{
	assert(device);
	assert(commandPool);
	assert(pCommandBuffers);

	_commandPool* cp = (_commandPool*)commandPool;

	for(int c = 0; c < commandBufferCount; ++c)
	{
		//if(cp->usePoolAllocator)
		{
			consecutivePoolFree(&cp->cpa, &pCommandBuffers[c]->binCl, pCommandBuffers[c]->binCl.numBlocks);
			consecutivePoolFree(&cp->cpa, &pCommandBuffers[c]->handlesCl, pCommandBuffers[c]->binCl.numBlocks);
			consecutivePoolFree(&cp->cpa, &pCommandBuffers[c]->shaderRecCl, pCommandBuffers[c]->binCl.numBlocks);
			consecutivePoolFree(&cp->cpa, &pCommandBuffers[c]->uniformsCl, pCommandBuffers[c]->binCl.numBlocks);
			poolFree(&cp->pa, pCommandBuffers[c]);
		}
	}
}

/*
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#vkDestroyCommandPool
 * When a pool is destroyed, all command buffers allocated from the pool are freed.
 * Any primary command buffer allocated from another VkCommandPool that is in the recording or executable state and has a secondary command buffer
 * allocated from commandPool recorded into it, becomes invalid.
 */
VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(
		VkDevice                                    device,
		VkCommandPool                               commandPool,
		const VkAllocationCallbacks*                pAllocator)
{
	assert(device);
	assert(commandPool);

	//TODO: allocator is ignored for now
	assert(pAllocator == 0);

	_commandPool* cp = (_commandPool*)commandPool;

	//if(cp->usePoolAllocator)
	{
		free(cp->pa.buf);
		free(cp->cpa.buf);
		destroyPoolAllocator(&cp->pa);
		destroyConsecutivePoolAllocator(&cp->cpa);
	}

	free(cp);
}

void clFit(VkCommandBuffer cb, ControlList* cl, uint32_t commandSize)
{
	if(!clHasEnoughSpace(cl, commandSize))
	{
		uint32_t currSize = clSize(cl);
		cl->buffer = consecutivePoolReAllocate(&cb->cp->cpa, cl->buffer, cl->numBlocks); assert(cl->buffer);
		cl->nextFreeByte = cl->buffer + currSize;
	}
}

void clDump(void* cl, uint32_t size)
{
		struct v3d_device_info devinfo = {
				/* While the driver supports V3D 2.1 and 2.6, we haven't split
				 * off a 2.6 XML yet (there are a couple of fields different
				 * in render target formatting)
				 */
				.ver = 21,
		};
		struct v3d_spec* spec = v3d_spec_load(&devinfo);

		struct clif_dump *clif = clif_dump_init(&devinfo, stderr, true);

		uint32_t offset = 0, hw_offset = 0;
		uint8_t *p = cl;

		while (offset < size) {
				struct v3d_group *inst = v3d_spec_find_instruction(spec, p);
				uint8_t header = *p;
				uint32_t length;

				if (inst == NULL) {
						printf("0x%08x 0x%08x: Unknown packet 0x%02x (%d)!\n",
								offset, hw_offset, header, header);
						return;
				}

				length = v3d_group_get_length(inst);

				printf("0x%08x 0x%08x: 0x%02x %s\n",
						offset, hw_offset, header, v3d_group_get_name(inst));

				v3d_print_group(clif, inst, offset, p);

				switch (header) {
				case VC4_PACKET_HALT:
				case VC4_PACKET_STORE_MS_TILE_BUFFER_AND_EOF:
						return;
				default:
						break;
				}

				offset += length;
				if (header != VC4_PACKET_GEM_HANDLES)
						hw_offset += length;
				p += length;
		}

		clif_dump_destroy(clif);
}