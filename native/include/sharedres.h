#ifndef SHAREDRES_H
#define SHAREDRES_H

#ifndef SWIG
#include <limits.h>
#include <assert.h>

#include <vector>
#include <algorithm>
#endif

typedef enum {
	WRITE = 0,
	READ  = 1,
} request_type_t;

class TaskInfo;

class RequestBound
{
private:
	unsigned int resource_id;
	unsigned int num_requests;
	unsigned int request_length;
	const TaskInfo*    task;
	request_type_t request_type;

public:
	RequestBound(unsigned int res_id,
		     unsigned int num,
		     unsigned int length,
		     const TaskInfo* tsk,
		     request_type_t type = WRITE)
		: resource_id(res_id),
		  num_requests(num),
		  request_length(length),
		  task(tsk),
		  request_type(type)
	{}

	unsigned int get_max_num_requests(unsigned long interval) const;

	unsigned int get_resource_id() const { return resource_id; }
	unsigned int get_num_requests() const { return num_requests; }
	unsigned int get_request_length() const { return request_length; }

	request_type_t get_request_type() const { return request_type; }

	bool is_read() const { return get_request_type() == READ; }
	bool is_write() const { return get_request_type() == WRITE; }

	const TaskInfo* get_task() const { return task; }
};

typedef std::vector<RequestBound> Requests;

class TaskInfo
{
private:
	unsigned int  priority;
	unsigned long period;
	unsigned long response;
	unsigned int  cluster;

	Requests requests;

public:
	TaskInfo(unsigned long _period,
		 unsigned long _response,
		 unsigned int _cluster,
		 unsigned int _priority)
		: priority(_priority),
		  period(_period),
		  response(_response),
		  cluster(_cluster)
	{}

	void add_request(unsigned int res_id,
			 unsigned int num,
			 unsigned int length,
			 request_type_t type = WRITE)
	{
		requests.push_back(RequestBound(res_id, num, length, this, type));
	}

	const Requests& get_requests() const
	{
		return requests;
	}

	unsigned int  get_priority() const { return priority; }
	unsigned long get_period() const { return period; }
	unsigned long get_response() const { return response; }
	unsigned int  get_cluster() const { return cluster; }

	unsigned int get_num_arrivals() const
	{
		return get_total_num_requests() + 1; // one for the job release
	}

	unsigned int get_total_num_requests() const
	{
		unsigned int count = 0;
		Requests::const_iterator it;
		for (it = requests.begin();
		     it != requests.end();
		     it++)
			count += it->get_num_requests();
		return count;
	}

	unsigned int get_max_request_length() const
	{
		unsigned int len = 0;
		Requests::const_iterator it;
		for (it = requests.begin();
		     it != requests.end();
		     it++)
			len = std::max(len, it->get_request_length());
		return len;
	}

};

typedef std::vector<TaskInfo> TaskInfos;

class ResourceSharingInfo
{
private:
	TaskInfos tasks;

public:
	ResourceSharingInfo(unsigned int num_tasks)
	{
		// Make sure all tasks will fit without re-allocation.
		tasks.reserve(num_tasks);
	}

	const TaskInfos& get_tasks() const
	{
		return tasks;
	}

	void add_task(unsigned long period,
		      unsigned long response,
		      unsigned int cluster  = 0,
		      unsigned int priority = UINT_MAX)
	{
		// Avoid re-allocation!
		assert(tasks.size() < tasks.capacity());
		tasks.push_back(TaskInfo(period, response, cluster, priority));
	}

	void add_request(unsigned int resource_id,
			 unsigned int max_num,
			 unsigned int max_length)
	{
		assert(!tasks.empty());

		TaskInfo& last_added = tasks.back();
		last_added.add_request(resource_id, max_num, max_length);
	}

	void add_request_rw(unsigned int resource_id,
			    unsigned int max_num,
			    unsigned int max_length,
			    int type)
	{
		assert(!tasks.empty());
		assert(type == WRITE || type == READ);

		TaskInfo& last_added = tasks.back();
		last_added.add_request(resource_id, max_num, max_length, (request_type_t) type);
	}

};


#define NO_CPU (-1)

class ResourceLocality
{
private:
	std::vector<int> mapping;

public:
	void assign_resource(unsigned int res_id, unsigned int processor)
	{
		while (mapping.size() <= res_id)
			mapping.push_back(NO_CPU);

		mapping[res_id] = processor;
	}

	int operator[](unsigned int res_id) const
	{
		if (mapping.size() <= res_id)
			return NO_CPU;
		else
			return mapping[res_id];
	}

};

class ReplicaInfo
{
private:
	std::vector<unsigned int> num_replicas;

public:
	void set_replicas(unsigned int res_id, unsigned int replicas)
	{
		assert(replicas >= 1);

		while (num_replicas.size() <= res_id)
			num_replicas.push_back(1); // default: not replicated

		num_replicas[res_id] = replicas;
	}

	unsigned int operator[](unsigned int res_id) const
	{
		if (num_replicas.size() <= res_id)
			return 1; // default: not replicated
		else
			return num_replicas[res_id];
	}
};

struct Interference
{
	unsigned int  count;
	unsigned long total_length;

	Interference() : count(0), total_length(0) {}

	Interference& operator+=(const Interference& other)
	{
		count        += other.count;
		total_length += other.total_length;
		return *this;
	}

	Interference operator+(const Interference& other) const
	{
		Interference result;
		result.count = this->count + other.count;
		result.total_length = this->total_length + other.total_length;
		return result;
	}

	bool operator<(const Interference& other) const
	{
		return total_length < other.total_length ||
			(total_length == other.total_length
			 && count < other.count);
	}
};

class BlockingBounds
{
private:
	std::vector<Interference> blocking;
	std::vector<Interference> request_span;
	std::vector<Interference> arrival;
	std::vector<Interference> remote;
	std::vector<Interference> local;

public:
	BlockingBounds(unsigned int num_tasks)
		: blocking(num_tasks),
		  request_span(num_tasks)
	{}

	BlockingBounds(const ResourceSharingInfo& info)
		:  blocking(info.get_tasks().size()),
		request_span(info.get_tasks().size()),
		arrival(info.get_tasks().size()),
		remote(info.get_tasks().size()),
		local(info.get_tasks().size())
	{}

	const Interference& operator[](unsigned int idx) const
	{
		assert( idx < size() );
		return blocking[idx];
	}

	Interference& operator[](unsigned int idx)
	{
		assert( idx < size() );
		return blocking[idx];
	}

	void raise_request_span(unsigned idx, const Interference& val)
	{
		assert( idx < size() );
		request_span[idx] = std::max(request_span[idx], val);
	}

	const Interference& get_max_request_span(unsigned idx) const
	{
		assert( idx < size() );
		return request_span[idx];
	}

	size_t size() const
	{
		return blocking.size();
	}

	unsigned long get_blocking_term(unsigned int tsk_index) const
	{
		assert( tsk_index < blocking.size() );
		return blocking[tsk_index].total_length;
	}

	unsigned long get_blocking_count(unsigned int tsk_index) const
	{
		assert( tsk_index < blocking.size() );
		return blocking[tsk_index].count;
	}

	unsigned long get_span_term(unsigned int tsk_index) const
	{
		assert( tsk_index < blocking.size() );
		return request_span[tsk_index].total_length;
	}

	unsigned long get_span_count(unsigned int tsk_index) const
	{
		assert( tsk_index < blocking.size() );
		return request_span[tsk_index].count;
	}


	unsigned long get_remote_blocking(unsigned int tsk_index) const
	{
		assert( tsk_index < remote.size() );
		return remote[tsk_index].total_length;
	}

	unsigned long get_remote_count(unsigned int tsk_index) const
	{
		assert( tsk_index < remote.size() );
		return remote[tsk_index].count;
	}

	void set_remote_blocking(unsigned int tsk_index,
				 const Interference& inf)
	{
		assert( tsk_index < remote.size() );
		remote[tsk_index] = inf;
	}

	unsigned long get_local_blocking(unsigned int tsk_index) const
	{
		assert( tsk_index < local.size() );
		return local[tsk_index].total_length;
	}

	unsigned long get_local_count(unsigned int tsk_index) const
	{
		assert( tsk_index < local.size() );
		return local[tsk_index].count;
	}

	void set_local_blocking(unsigned int tsk_index,
				const Interference& inf)
	{
		assert( tsk_index < local.size() );
		local[tsk_index] = inf;
	}

	unsigned long get_arrival_blocking(unsigned int tsk_index) const
	{
		assert( tsk_index < arrival.size() );
		return arrival[tsk_index].total_length;
	}

	void set_arrival_blocking(unsigned int tsk_index,
				  const Interference& inf)
	{
		assert( tsk_index < arrival.size() );
		arrival[tsk_index] = inf;
	}
};

// spinlocks

BlockingBounds* task_fair_mutex_bounds(const ResourceSharingInfo& info,
				       unsigned int procs_per_cluster,
				       int dedicated_irq = NO_CPU);

BlockingBounds* task_fair_rw_bounds(const ResourceSharingInfo& info,
				    const ResourceSharingInfo& info_mtx,
				    unsigned int procs_per_cluster,
				    int dedicated_irq = NO_CPU);

BlockingBounds* phase_fair_rw_bounds(const ResourceSharingInfo& info,
				     unsigned int procs_per_cluster,
				     int dedicated_irq = NO_CPU);

// s-oblivious protocols

BlockingBounds* global_omlp_bounds(const ResourceSharingInfo& info,
				   unsigned int num_procs);
BlockingBounds* global_fmlp_bounds(const ResourceSharingInfo& info);

BlockingBounds* clustered_omlp_bounds(const ResourceSharingInfo& info,
				      unsigned int procs_per_cluster,
				      int dedicated_irq = NO_CPU);

BlockingBounds* clustered_rw_omlp_bounds(const ResourceSharingInfo& info,
					 unsigned int procs_per_cluster,
					 int dedicated_irq = NO_CPU);

BlockingBounds* clustered_kx_omlp_bounds(const ResourceSharingInfo& info,
					 const ReplicaInfo& replicaInfo,
					 unsigned int procs_per_cluster,
					 int dedicated_irq);

BlockingBounds* part_omlp_bounds(const ResourceSharingInfo& info);


// s-aware protocols

BlockingBounds* part_fmlp_bounds(const ResourceSharingInfo& info,
				 bool preemptive = true);

BlockingBounds* mpcp_bounds(const ResourceSharingInfo& info,
			    bool use_virtual_spinning);

BlockingBounds* dpcp_bounds(const ResourceSharingInfo& info,
			    const ResourceLocality& locality);

// Still missing:
// ==============

// clustered_omlp_kex_bounds
// clustered_omlp_rw_bounds
// spin_rw_wpref_bounds
// spin_rw_rpref_bounds



#endif
