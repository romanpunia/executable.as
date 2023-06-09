#include "../deps/mavi.as/src/runtime.h"
#include "program.hpp"
#include <signal.h>

EventLoop* Loop = nullptr;
VirtualMachine* VM = nullptr;
Compiler* Unit = nullptr;
ImmediateContext* Context = nullptr;
Schedule* Queue = nullptr;
int ExitCode = 0;

void exit_program(int sigv)
{
	if (sigv != SIGINT && sigv != SIGTERM)
        return;
    {
        if (TryContextExit(ProgramContext::Get(), sigv))
        {
			Loop->Wakeup();
            goto GracefulShutdown;
        }

        auto* App = Application::Get();
        if (App != nullptr && App->GetState() == ApplicationState::Active)
        {
            App->Stop();
			Loop->Wakeup();
            goto GracefulShutdown;
        }

        auto* Queue = Schedule::Get();
        if (Queue->IsActive())
        {
            Queue->Stop();
			Loop->Wakeup();
            goto GracefulShutdown;
        }

        return std::exit(JUMP_CODE + EXIT_KILL);
    }
GracefulShutdown:
    signal(sigv, &exit_program);
}
void setup_program(ProgramContext& Contextual)
{
    OS::Directory::SetWorking(Contextual.Path.c_str());
    signal(SIGINT, &exit_program);
    signal(SIGTERM, &exit_program);
#ifdef VI_UNIX
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);
#endif
}
bool load_program(ProgramContext& Contextual)
{
#ifdef HAS_PROGRAM_HEX
    program_hex::foreach(&Contextual, [](void* Context, const char* Buffer, unsigned Size)
    {
        ProgramContext* Contextual = (ProgramContext*)Context;
	    Contextual->Program = Codec::HexDecode(Buffer, (size_t)Size);
    });
    return true;
#else
    return false;
#endif
}
int main(int argc, char* argv[])
{
	ProgramContext Contextual(argc, argv);
	Contextual.Path = *OS::Directory::GetModule();
	Contextual.Module = argc > 0 ? argv[0] : "runtime";
    if (!load_program(Contextual))
        return 0;

	ProgramConfig Config;
	Config.Libraries = { {{BUILDER_CONFIG_LIBRARIES}} };
	Config.Functions = { {{BUILDER_CONFIG_FUNCTIONS}} };
	Config.SystemAddons = { {{BUILDER_CONFIG_ADDONS}} };
	Config.CLibraries = {{BUILDER_CONFIG_CLIBRARIES}};
	Config.CFunctions = {{BUILDER_CONFIG_CFUNCTIONS}};
	Config.Addons = {{BUILDER_CONFIG_SYSTEM_ADDONS}};
	Config.Files = {{BUILDER_CONFIG_FILES}};
	Config.Remotes = {{BUILDER_CONFIG_REMOTES}};
	Config.Translator = {{BUILDER_CONFIG_TRANSLATOR}};
	Config.EssentialsOnly = {{BUILDER_CONFIG_ESSENTIALS_ONLY}};
    setup_program(Contextual);

	Mavi::Runtime Scope(Config.EssentialsOnly ? (size_t)Mavi::Preset::App : (size_t)Mavi::Preset::Game);
	{
		VM = new VirtualMachine();
		Unit = VM->CreateCompiler();
        Context = VM->RequestContext();
		Queue = Schedule::Get();
		Queue->SetImmediate(true);
		
        Vector<std::pair<uint32_t, size_t>> Settings = { {{BUILDER_CONFIG_SETTINGS}} };
        for (auto& Item : Settings)
            VM->SetProperty((Features)Item.first, Item.second);

		Unit = VM->CreateCompiler();
		ExitCode = ConfigureEngine(Config, Contextual, VM, Unit);
		if (ExitCode != 0)
			goto FinishProgram;

		if (!Unit->Prepare(Contextual.Module))
		{
			VI_ERR("cannot prepare <%s> module scope", Contextual.Module);
			ExitCode = JUMP_CODE + EXIT_PREPARE_FAILURE;
			goto FinishProgram;
		}

		ByteCodeInfo Info;
		Info.Data.insert(Info.Data.begin(), Contextual.Program.begin(), Contextual.Program.end());
		if (!Unit->LoadByteCode(&Info).Get())
		{
			VI_ERR("cannot load <%s> module bytecode", Contextual.Module);
			ExitCode = JUMP_CODE + EXIT_LOADING_FAILURE;
			goto FinishProgram;
		}

	    ProgramEntrypoint Entrypoint;
		Function Main = GetEntrypoint(Contextual, Entrypoint, Unit);
		if (!Main.IsValid())
        {
			ExitCode = JUMP_CODE + EXIT_ENTRYPOINT_FAILURE;
			goto FinishProgram;
        }

		int ExitCode = 0;
		TypeInfo Type = VM->GetTypeInfoByDecl("array<string>@");
		Bindings::Array* ArgsArray = Type.IsValid() ? Bindings::Array::Compose<String>(Type.GetTypeInfo(), Contextual.Args) : nullptr;
		VM->SetExceptionCallback([](ImmediateContext* Context)
		{
			if (!Context->WillExceptionBeCaught())
				std::exit(JUMP_CODE + EXIT_RUNTIME_FAILURE);
		});

		Main.AddRef();
		Loop = new EventLoop();
		Loop->Listen(Context);
		Loop->Enqueue(FunctionDelegate(Main, Context), [&Main, ArgsArray](ImmediateContext* Context)
		{
			if (Main.GetArgsCount() > 0)
				Context->SetArgObject(0, ArgsArray);
		}, [&ExitCode, &Type, &Main, ArgsArray](ImmediateContext* Context)
		{
			ExitCode = Main.GetReturnTypeId() == (int)TypeId::VOIDF ? 0 : (int)Context->GetReturnDWord();
			if (ArgsArray != nullptr)
				Context->GetVM()->ReleaseObject(ArgsArray, Type);
		});
        
		AwaitContext(Queue, Loop, VM, Context);
	}
FinishProgram:
	VI_RELEASE(Context);
	VI_RELEASE(Unit);
	VI_RELEASE(VM);
    VI_RELEASE(Loop);
	VI_RELEASE(Queue);
	return ExitCode;
}