#include "pooled_evaluator.h"
#include "xo/system/log.h"
#include <iostream>

namespace spot
{
	pooled_evaluator::pooled_evaluator( size_t max_threads, xo::thread_priority thread_prio ) :
		max_threads_( max_threads ),
		thread_prio_( thread_prio )
	{
		start_threads();
	}

	pooled_evaluator::~pooled_evaluator()
	{
		stop_threads();
	}

	vector< result<fitness_t> > pooled_evaluator::evaluate( const objective& o, const search_point_vec& point_vec, priority_t prio )
	{
		// prepare vector of tasks and futures
		vector< std::future< result<fitness_t> > > futures;
		futures.reserve( point_vec.size() );
		vector< eval_task > tasks;
		tasks.reserve( point_vec.size() );
		for ( const auto& point : point_vec )
		{
			tasks.emplace_back( [&]() { return evaluate_noexcept( o, point ); } );
			futures.emplace_back( tasks.back().get_future() );
		}

		{
			// add tasks to end of queue
			std::scoped_lock lock( queue_mutex_ );
			std::move( tasks.begin(), tasks.end(), std::back_inserter( queue_ ) );
			std::cout << queue_.size() << std::endl;
		}

		// worker threads are notified after the lock is released
		queue_cv_.notify_all();
		tasks.clear(); // these tasks are moved-out and cleared for clarity

		vector< result<fitness_t> > results;
		results.reserve( point_vec.size() );
		for ( auto& f : futures )
			results.push_back( f.get() );
		return results;
	}

	void pooled_evaluator::set_max_threads( size_t thread_count, xo::thread_priority prio )
	{
		if ( max_threads_ != thread_count || thread_prio_ != prio )
		{
			max_threads_ = thread_count;
			stop_threads();
			start_threads();
		}
;	}

	void pooled_evaluator::start_threads()
	{
		if ( !threads_.empty() )
			stop_threads();
		stop_signal_ = false;
		for ( index_t i = 0; i < max_threads_; ++i )
			threads_.emplace_back( &pooled_evaluator::thread_func, this );
	}

	void pooled_evaluator::stop_threads()
	{
		stop_signal_ = true;
		queue_cv_.notify_all();
		while ( !threads_.empty() )
		{
			threads_.back().join();
			threads_.pop_back();
		}
	}

	void pooled_evaluator::thread_func()
	{
		std::cout << "starting thread " << std::this_thread::get_id() << "\n";
		while ( !stop_signal_ )
		{
			eval_task task;
			{
				std::unique_lock lock( queue_mutex_ );
				while ( queue_.empty() )
				{
					queue_cv_.wait( lock );
					if ( stop_signal_ )
						return;
				}
				task = std::move( queue_.back() );
				queue_.pop_back();
				std::cout << queue_.size() << " by " << std::this_thread::get_id() << std::endl;
			}
			task();
		}
		std::cout << "stopping thread " << std::this_thread::get_id() << "\n";
	}
}