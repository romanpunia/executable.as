#include "../deps/mavi.as/src/runtime.h"
#include "program.hpp"

int main(int argc, char* argv[])
{
	int ExitCode = 0;
	Schedule* Queue;
	VirtualMachine* VM;
	Compiler* Unit;
	ProgramContext Contextual(argc, argv);
	Contextual.Path = argc > 0 ? argv[0] : "";
	Contextual.Module = OS::Path::GetFilename(Contextual.Path.c_str());
#ifdef HAS_PROGRAM_HEX
    program_hex::foreach(&Contextual, [](void* Context, const char* Buffer, unsigned Size)
    {
        ProgramContext* Contextual = (ProgramContext*)Context;
	    Contextual->Program = Codec::HexDecode(Buffer, (size_t)Size);
    });
#else
    return -1;
#endif
    Vector<std::pair<uint32_t, size_t>> Settings = { {{BUILDER_CONFIG_SETTINGS}}};
	ProgramEntrypoint Entrypoint;
	ProgramConfig Config;
	Config.Libraries = { {{BUILDER_CONFIG_LIBRARIES}} };
	Config.Functions = { {{BUILDER_CONFIG_FUNCTIONS}} };
	Config.SystemAddons = { {{BUILDER_CONFIG_ADDONS}} };
	Config.CLibraries = {{BUILDER_CONFIG_CLIBRARIES}};
	Config.CSymbols = {{BUILDER_CONFIG_CSYMBOLS}};
	Config.Addons = {{BUILDER_CONFIG_SYSTEM_ADDONS}};
	Config.Files = {{BUILDER_CONFIG_FILES}};
	Config.Remotes = {{BUILDER_CONFIG_REMOTES}};
	Config.Translator = {{BUILDER_CONFIG_TRANSLATOR}};
	Config.EssentialsOnly = {{BUILDER_CONFIG_ESSENTIALS_ONLY}};

	Mavi::Initialize(Config.EssentialsOnly ? (size_t)Mavi::Preset::App : (size_t)Mavi::Preset::Game);
	{
		VM = new VirtualMachine();
		Unit = VM->CreateCompiler();

		Queue = Schedule::Get();
		Queue->SetImmediate(true);
		Multiplexer::Create();

        for (auto& Item : Settings)
            VM->SetProperty((Features)Item.first, Item.second);

		ExitCode = ConfigureEngine(Config, Contextual, VM);
		if (ExitCode != 0)
			goto FinishProgram;

		Unit = VM->CreateCompiler();
		if (Unit->Prepare(Contextual.Module) < 0)
		{
			VI_ERR("cannot prepare <%s> module scope", Contextual.Module);
			return JUMP_CODE + EXIT_PREPARE_FAILURE;
		}

		ByteCodeInfo Info;
		Info.Data.insert(Info.Data.begin(), Contextual.Program.begin(), Contextual.Program.end());
		if (Unit->LoadByteCode(&Info).Get() < 0)
		{
			VI_ERR("cannot load <%s> module bytecode", Contextual.Module);
			return JUMP_CODE + EXIT_LOADING_FAILURE;
		}

		Function Main = GetEntrypoint(Contextual, Entrypoint, Unit);
		if (!Main.IsValid())
			return JUMP_CODE + EXIT_ENTRYPOINT_FAILURE;

		ImmediateContext* Context = Unit->GetContext();
		Context->SetExceptionCallback([](ImmediateContext* Context)
		{
			if (!Context->WillExceptionBeCaught())
				std::exit(JUMP_CODE + EXIT_RUNTIME_FAILURE);
		});

		TypeInfo Type = VM->GetTypeInfoByDecl("array<string>@");
		Bindings::Array* ArgsArray = Bindings::Array::Compose<String>(Type.GetTypeInfo(), Contextual.Args);
		Context->Execute(Main, [&Main, ArgsArray](ImmediateContext* Context)
		{
			if (Main.GetArgsCount() > 0)
				Context->SetArgObject(0, ArgsArray);
		}).Wait();

		int ExitCode = Main.GetReturnTypeId() == (int)TypeId::VOIDF ? 0 : (int)Context->GetReturnDWord();
		VM->ReleaseObject(ArgsArray, Type);
		AwaitContext(Queue, VM, Context);
		return ExitCode;
	}
FinishProgram:
	VI_RELEASE(Unit);
	VI_RELEASE(VM);
	Mavi::Uninitialize();
	return ExitCode;
}