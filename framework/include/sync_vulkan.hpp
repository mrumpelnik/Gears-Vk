#pragma once

namespace cgb
{
	// Forward-declare the device-queue
	class device_queue;
	class window;

	/**	The sync class is a fundamental part of the framework and is used wherever synchronization is or can be needed.
	 *	It allows a caller to inject a specific synchronization strategy into a particular method/function.
	 */
	class sync
	{
	public:
		enum struct sync_type { not_required, via_wait_idle, via_semaphore, via_barrier };
		using steal_before_handler_t = void(*)(command_buffer_t&, pipeline_stage, std::optional<read_memory_access>);
		using steal_after_handler_t = void(*)(command_buffer_t&, pipeline_stage, std::optional<write_memory_access>);
		static void steal_before_handler(command_buffer_t&, pipeline_stage, std::optional<read_memory_access>) {}
		static void steal_after_handler(command_buffer_t&, pipeline_stage, std::optional<write_memory_access>) {}
		static bool is_before_handler_stolen(const std::function<void(command_buffer_t&, pipeline_stage, std::optional<read_memory_access>)>& aToTest) {
			const auto trgPtr = aToTest.target<steal_before_handler_t>();
			return nullptr == trgPtr ? false : *trgPtr == steal_before_handler ? true : false;
		}
		static bool is_after_handler_stolen(const std::function<void(command_buffer_t&, pipeline_stage, std::optional<write_memory_access>)>& aToTest) {
			const auto trgPtr = aToTest.target<steal_after_handler_t>();
			return nullptr == trgPtr ? false : *trgPtr == steal_after_handler ? true : false;
		}
		
		sync() = default;
		sync(const sync&) = delete;
		sync(sync&&) noexcept = default;
		sync& operator=(const sync&) = delete;
		sync& operator=(sync&&) noexcept = default;

#pragma region static creation functions
		static void default_handler_before_operation(command_buffer_t& aCommandBuffer, pipeline_stage aDestinationStage, std::optional<read_memory_access> aDestinationAccess)
		{
			// We do not know which operation came before. Hence, we have to be overly cautious and
			// establish a (possibly) hefty barrier w.r.t. write access that happened before.
			aCommandBuffer.establish_global_memory_barrier(
				pipeline_stage::all_commands,	 aDestinationStage,	// Wait for all previous command before continuing with the operation's command
				memory_access::any_write_access, aDestinationAccess	// Make any write access available before making the operation's read access type visible
			);
		}
		
		static void default_handler_after_operation(command_buffer_t& aCommandBuffer, pipeline_stage aSourceStage, std::optional<write_memory_access> aSourceAccess)
		{
			// We do not know which operation comes after. Hence, we have to be overly cautious and
			// establish a (possibly) hefty barrier w.r.t. read access that happens after.
			aCommandBuffer.establish_global_memory_barrier(
				aSourceStage,	pipeline_stage::all_commands,	// All subsequent stages have to wait until the operation has completed
				aSourceAccess, 	memory_access::any_read_access	// Make the operation's writes available and visible to all memory stages
			);
		}

		/**	Indicate that no sync is required. If you are wrong, there will be an exception.
		 */
		static sync not_required();
		
		/**	Establish very coarse (and inefficient) synchronization by waiting for the queue to become idle before continuing.
		 */
		static sync wait_idle();

		/**	Establish semaphore-based synchronization with a custom semaphore lifetime handler.
		 *	@param	aSignalledAfterOperation	A function to handle the lifetime of a created semaphore. 
		 *	@param	aWaitBeforeOperation		A vector of other semaphores to be waited on before executing the command.
		 */
		static sync with_semaphores(std::function<void(semaphore)> aSignalledAfterOperation, std::vector<semaphore> aWaitBeforeOperation = {});

		/**	Establish semaphore-based synchronization and have its lifetime handled w.r.t the window's swap chain.
		 *	@param	aWaitBeforeOperation		A vector of other semaphores to be waited on before executing the command.
		 *	@param	aWindow				A window, whose swap chain shall be used to handle the lifetime of the possibly emerging semaphore.
		 */
		static sync with_semaphores_on_current_frame(std::vector<semaphore> aWaitBeforeOperation = {}, cgb::window* aWindow = nullptr);

		/**	Establish barrier-based synchronization with a custom command buffer lifetime handler.
		 *	@param	aCommandBufferLifetimeHandler	A function to handle the lifetime of a command buffer.
		 */
		static sync with_barriers(
			std::function<void(command_buffer)> aCommandBufferLifetimeHandler,
			std::function<void(command_buffer_t&, pipeline_stage /* destination stage */, std::optional<read_memory_access> /* destination access */)> aEstablishBarrierBeforeOperation = {},
			std::function<void(command_buffer_t&, pipeline_stage /* source stage */,	  std::optional<write_memory_access> /* source access */)> aEstablishBarrierAfterOperation = default_handler_after_operation
		);

		/**	Establish barrier-based synchronization with a custom command buffer lifetime handler.
		 *	@param	aCommandBufferLifetimeHandler	A function to handle the lifetime of a command buffer.
		 */
		static sync with_barriers_on_current_frame(
			std::function<void(command_buffer_t&, pipeline_stage /* destination stage */, std::optional<read_memory_access> /* destination access */)> aEstablishBarrierBeforeOperation = {},
			std::function<void(command_buffer_t&, pipeline_stage /* source stage */,	  std::optional<write_memory_access> /* source access */)> aEstablishBarrierAfterOperation = default_handler_after_operation,
			cgb::window* aWindow = nullptr
		);

		/**	Establish barrier-based synchronization for a command which is subordinate to a
		 *	"master"-sync handler. The master handler is usually provided by the user and this
		 *	method is used to create sync objects which go along with the master sync, i.e.,
		 *	lifetime of subordinate operations' command buffers are handled along with the
		 *	master handler.
		 *
		 *	@param	aMasterSync		Master sync handler which is being modified by this method
		 *							in order to also handle lifetime of subordinate command buffers.
		 */
		static sync auxiliary(
			sync& aMasterSync,
			std::function<void(command_buffer_t&, pipeline_stage /* destination stage */, std::optional<read_memory_access> /* destination access */)> aEstablishBarrierBeforeOperation,
			std::function<void(command_buffer_t&, pipeline_stage /* source stage */, std::optional<write_memory_access> /* source access */)> aEstablishBarrierAfterOperation
		);
#pragma endregion 

#pragma region ownership-related settings
		/**	Set the queue where the command is to be submitted to AND also where the sync will happen.
		 */
		sync& on_queue(std::reference_wrapper<device_queue> aQueue);
#pragma endregion 

#pragma region getters 
		/** Determine the fundamental sync approach configured in this `sync`. */
		sync_type get_sync_type() const;
		
		/** Queue which the command and sync will be submitted to. */
		std::reference_wrapper<device_queue> queue_to_use() const;
#pragma endregion 

#pragma region essential functions which establish the actual sync. Used by the framework internally.
		void set_queue_hint(std::reference_wrapper<device_queue> aQueueRecommendation);
		
		void establish_barrier_before_the_operation(command_buffer_t& aCommandBuffer, pipeline_stage aDestinationPipelineStages, std::optional<read_memory_access> aDestinationMemoryStages) const;
		void establish_barrier_after_the_operation(command_buffer_t& aCommandBuffer, pipeline_stage aSourcePipelineStages, std::optional<write_memory_access> aSourceMemoryStages) const;

		/**	Submit the command buffer and engage sync!
		 *	This method is intended not to be used by framework-consuming code, but by the framework-internals.
		 *	Whichever synchronization strategy has been configured for this `cgb::sync`, it will be executed here
		 *	(i.e. waiting idle, establishing a barrier, or creating a semaphore).
		 *
		 *	@param	aCommandBuffer				Hand over ownership of a command buffer in a "fire and forget"-manner from this method call on.
		 *										The command buffer will be submitted to a queue (whichever queue is configured in this `cgb::sync`)
		 */
		void submit_and_sync(command_buffer aCommandBuffer);

		void sync_with_dummy_command_buffer();
#pragma endregion
		
	private:
		bool mNoSyncRequired = false;
		std::function<void(semaphore)> mSemaphoreSignalAfterAndLifetimeHandler;
		std::vector<semaphore> mWaitBeforeSemaphores;
		std::function<void(command_buffer)> mCommandBufferLifetimeHandler;
		std::function<void(command_buffer_t&, pipeline_stage /* destination stage */, std::optional<read_memory_access> /* destination access */)> mEstablishBarrierBeforeOperationCallback;
		std::function<void(command_buffer_t&, pipeline_stage /* source stage */,	  std::optional<write_memory_access> /* source access */)>	  mEstablishBarrierAfterOperationCallback;
		std::optional<std::reference_wrapper<device_queue>> mQueueToUse;
	};
}