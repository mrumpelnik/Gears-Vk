#include <cg_base.hpp>

namespace cgb
{
	semaphore_t::semaphore_t()
		: mCreateInfo{}
		, mSemaphore{}
		, mSemaphoreWaitStageForNextCommand{ vk::PipelineStageFlagBits::eAllCommands }
		, mCustomDeleter{}
	{
	}

	semaphore_t::~semaphore_t()
	{
		if (mCustomDeleter.has_value() && *mCustomDeleter) {
			// If there is a custom deleter => call it now
			(*mCustomDeleter)();
			mCustomDeleter.reset();
		}
		// Destroy the dependant instance before destroying myself
		// ^ This is ensured by the order of the members
		//   See: https://isocpp.org/wiki/faq/dtors#calling-member-dtors
	}

	semaphore_t& semaphore_t::set_semaphore_wait_stage(vk::PipelineStageFlags _Stage)
	{
		mSemaphoreWaitStageForNextCommand = _Stage;
		return *this;
	}

	cgb::owning_resource<semaphore_t> semaphore_t::create(context_specific_function<void(semaphore_t&)> _AlterConfigBeforeCreation)
	{ 
		semaphore_t result;
		result.mCreateInfo = vk::SemaphoreCreateInfo{};

		// Maybe alter the config?
		if (_AlterConfigBeforeCreation.mFunction) {
			_AlterConfigBeforeCreation.mFunction(result);
		}

		result.mSemaphore = context().logical_device().createSemaphoreUnique(result.mCreateInfo);
		return result;
	}
}
