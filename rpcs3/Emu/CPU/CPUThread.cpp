#include "stdafx.h"
#include "Emu/System.h"
#include "CPUThread.h"

#include <mutex>

template<>
void fmt_class_string<cpu_type>::format(std::string& out, u64 arg)
{
	format_enum(out, arg, [](auto arg)
	{
		switch (arg)
		{
		STR_CASE(cpu_type::ppu);
		STR_CASE(cpu_type::spu);
		STR_CASE(cpu_type::arm);
		}

		return unknown;
	});
}

template<>
void fmt_class_string<cpu_state>::format(std::string& out, u64 arg)
{
	format_enum(out, arg, [](cpu_state f)
	{
		switch (f)
		{
		STR_CASE(cpu_state::stop);
		STR_CASE(cpu_state::exit);
		STR_CASE(cpu_state::suspend);
		STR_CASE(cpu_state::ret);
		STR_CASE(cpu_state::signal);
		STR_CASE(cpu_state::dbg_global_pause);
		STR_CASE(cpu_state::dbg_global_stop);
		STR_CASE(cpu_state::dbg_pause);
		STR_CASE(cpu_state::dbg_step);
		case cpu_state::__bitset_enum_max: break;
		}

		return unknown;
	});
}

template<>
void fmt_class_string<bs_t<cpu_state>>::format(std::string& out, u64 arg)
{
	format_bitset(out, arg, "[", "|", "]", &fmt_class_string<cpu_state>::format);
}

thread_local cpu_thread* g_tls_current_cpu_thread = nullptr;

void cpu_thread::on_task()
{
	state -= cpu_state::exit;

	g_tls_current_cpu_thread = this;

	Emu.SendDbgCommand(DID_CREATE_THREAD, this);

	std::unique_lock<named_thread> lock(*this);

	// Check thread status
	while (!test(state & cpu_state::exit))
	{
		CHECK_EMU_STATUS;

		// check stop status
		if (!test(state & cpu_state::stop))
		{
			if (lock) lock.unlock();

			try
			{
				cpu_task();
			}
			catch (cpu_state _s)
			{
				state += _s;
			}
			catch (const std::exception&)
			{
				LOG_NOTICE(GENERAL, "\n%s", dump());
				throw;
			}

			state -= cpu_state::ret;
			continue;
		}

		if (!lock)
		{
			lock.lock();
			continue;
		}

		thread_ctrl::wait();
	}
}

void cpu_thread::on_stop()
{
	state += cpu_state::exit;
	lock_notify();
}

cpu_thread::~cpu_thread()
{
}

cpu_thread::cpu_thread(cpu_type type)
	: type(type)
{
}

bool cpu_thread::check_state()
{
	std::unique_lock<named_thread> lock(*this, std::defer_lock);

	while (true)
	{
		CHECK_EMU_STATUS; // check at least once

		if (test(state & cpu_state::exit))
		{
			return true;
		}

		if (!test(state & cpu_state_pause))
		{
			break;
		}

		if (!lock)
		{
			lock.lock();
			continue;
		}

		thread_ctrl::wait();
	}

	const auto state_ = state.load();

	if (test(state_, cpu_state::ret + cpu_state::stop))
	{
		return true;
	}

	if (test(state_, cpu_state::dbg_step))
	{
		state += cpu_state::dbg_pause;
		state -= cpu_state::dbg_step;
	}

	return false;
}

void cpu_thread::run()
{
	state -= cpu_state::stop;
	lock_notify();
}
