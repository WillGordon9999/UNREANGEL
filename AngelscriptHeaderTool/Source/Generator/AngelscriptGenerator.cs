// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using EpicGames.UHT.Tokenizer;
using System.Text;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Data.Common;
using UnrealBuildTool;
using System;
using System.Reflection.PortableExecutable;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Runtime.CompilerServices;
using static System.Net.Mime.MediaTypeNames;
using System.Runtime.InteropServices;
using Microsoft.Extensions.Logging.Abstractions;
//using AutomationTool;

namespace AngelscriptUHT;
public struct ModuleData
{
    public UhtPackage package;
    public HashSet<UhtPackage> dependencies;
    public List<string> fileNames;
    public List<string> sourceFiles;
    public List<string> funcDecls;
    public List<string> deleteFiles;
    public List<string> noAPIDebugs;

    public ModuleData(UhtPackage pack)
    {
        package = pack;
        dependencies = new HashSet<UhtPackage>();
        fileNames = new List<string>();
        sourceFiles = new List<string>();
        funcDecls = new List<string>();
        deleteFiles = new List<string>();
        noAPIDebugs = new List<string>();
    }

}

[Serializable]
public struct SkipData
{        
    public string className { get; set; }        
    public string functionName { get; set; }        
    public bool skipClass { get; set; }

    public SkipData(string className, string functionName, bool skipClass)
    {
        this.className = className;
        this.functionName = functionName;
        this.skipClass = skipClass;
    }        
}

[Serializable]
public class SkipList
{
    //[JsonInclude] this can apparently be used to serialize fields
    public bool useDebugMode { get; set; }
    public HashSet<SkipData> skips { get; set; }
    public HashSet<string> moduleSkips { get; set; }
    public SkipList()
    {        
        skips = new HashSet<SkipData>();
        moduleSkips = new HashSet<string>();
        useDebugMode = false;
    }
}

//A small struct to allow us to have multiple modules map to the same plugin in plugin dictionary
public struct ModuleToPlugin
{
    public string name { get; set; }
    public int ID { get; set; }
}


[UnrealHeaderTool]
class AngelscriptGenerator
{
    public static IUhtExportFactory export { get; set; }
    const string asInclude = "#include \"../Source/AngelscriptCode/Public/AngelscriptBinds.h\"";

    [UhtExporter
    (
        Name = "AngelscriptHeaderTool",
        Description = "Angelscript Bind Generator",
        Options = UhtExporterOptions.Default,
        ModuleName = "AngelscriptHeader"    
    )]
    public static void ScriptGeneratorExporter(IUhtExportFactory Factory)
    {
        //if (!Factory.Session.IsPluginEnabled("AngelscriptHeaderTool", false))
        //    return;

        //CORE VARS
        export = Factory;
        bool debugMode = false;
        const string editor = "Editor";
        const string angelscript = "Angelscript";        
        const string coreMin = "#include \"CoreMinimal.h\"";
        const string skipDir = @"\Plugins\Angelscript\SkipBinds.json";
        const string sourceDir = @"\Plugins\Angelscript\Source\";
        string mainDir = $@"{Factory.Session.ProjectDirectory}\Plugins\Angelscript\";
        string targetDir = Factory.Session.ProjectDirectory + sourceDir;
        List<string> publicDeps = new List<string>() { "Core", "CoreUObject", "Engine", "AngelscriptCode" };
        
        if (!Directory.Exists(Factory.Session.ProjectDirectory + @"\Script\"))
        {
            Factory.Session.LogInfo("Creating Script Folder");
            Directory.CreateDirectory(Factory.Session.ProjectDirectory + @"\Script\");
        }
        
        if (!Directory.Exists(mainDir))
        {
            Factory.Session.LogInfo($"Angelscript Plugin not found in Project {mainDir}");
            mainDir = $@"{Factory.Session.EngineDirectory}\Plugins\Angelscript\";
            return;
        }

        HashSet<string> nestTypeSet = new HashSet<string>();
        List<string> nestTypeNames = new List<string>();

        HashSet<string> argNames = new HashSet<string>();
        HashSet<string> argIncludes = new HashSet<string>();
        HashSet<UhtHeaderFile> includeTypes = new HashSet<UhtHeaderFile>();

        List<UhtToken> fnTokens = new List<UhtToken>();
        List<ModuleData> modules = new List<ModuleData>();
        //List<ModuleData> deleteModules = new List<ModuleData>();

        UhtPackage targetModule = null;
        string fullSkipDir = Factory.Session.ProjectDirectory + skipDir;
        string json = File.ReadAllText(fullSkipDir, Encoding.UTF8);

        //LOAD SKIP BINDS FILE
        JsonSerializerOptions jsonOptions = new JsonSerializerOptions();
        SkipList skipList = JsonSerializer.Deserialize<SkipList>(json, jsonOptions)!;
        if (skipList == null)
        {
            Factory.Session.LogInfo("Skip List failed to load");
            return;
        }

        debugMode = skipList.useDebugMode;
        SkipData classSkip = new SkipData("", "", true);
        SkipData funcSkip = new SkipData("", "", false);

        int acceptedTypes = 0;
        int acceptedFuncs = 0;
        int rejectedTypes = 0;
        int rejectedFuncs = 0;
        int rejectedModules = 0;

        UHTManifest manifest = Factory.Session.Manifest!;
        string targetName = manifest.TargetName;
        Factory.Session.LogInfo($"Manifest Name: {manifest.TargetName}");
        Factory.Session.LogInfo($"Project File: {Factory.Session.ProjectFile}");

        string projectName = Factory.Session.ProjectFile!;
        {
            int slash = projectName.LastIndexOf(@"\");
            int dot = projectName.LastIndexOf(".");            
            projectName = projectName.Substring(slash + 1, dot - slash - 1);
        }

        Factory.Session.LogInfo($"Project Name: {projectName}");
        
        ProjectDescriptor project = ProjectDescriptor.FromFile(FileReference.FromString(Factory.Session.ProjectFile!));

        if (project == null)
        {
            Factory.Session.LogInfo("Failed to Load Project File");
            return;
        }

        //Get Target Platform, Config, Architecture to find the correct target file to load

        UnrealTargetPlatform targetPlatform = BuildHostPlatform.Current.Platform;
        UnrealArchitectureConfig config = UnrealArchitectureConfig.ForPlatform(targetPlatform);
        TargetType targetType = TargetType.Game;
        UnrealTargetConfiguration targetConfig = UnrealTargetConfiguration.Unknown;

        string appStr = "";
        string targetStr = "";
        string platformStr = "";
        string configStr = "";

        DirectoryReference projectRef = new DirectoryReference(Factory.Session.ProjectDirectory!);
        DirectoryReference engineRef = new DirectoryReference(Factory.Session.EngineDirectory!);
        DirectoryReference dataPath = UnrealBuildBase.Unreal.UserSettingDirectory;

        {           
            //HACK: After much searching, so far the only reliable way I can find all this information
            //is by reading our own log file
            string newDir = dataPath.ToString();
            newDir += @"\UnrealBuildTool\Log.json";
            Factory.Session.LogInfo($"Log Path Test {newDir}");
            string log = "";
            using (FileStream logFileStream = new FileStream(newDir, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
            using (StreamReader logFileReader = new StreamReader(logFileStream))
            {
                try { log = logFileReader.ReadToEnd(); }
                catch { Factory.Session.LogInfo("failed log read"); }   
            }

            int build = log.IndexOf("Building ");
            if (build == -1)
            {
                Factory.Session.LogInfo("Failed to find Building entry in log");
                return;
            }
                        
            int appIndex = log.IndexOf("\"AppName\"", build);
            int targetIndex = log.IndexOf("\"TargetName\"", build);
            int platformIndex = log.IndexOf("\"Platform\"", build);
            int configIndex = log.IndexOf("\"Configuration\"", build);
            int close = log.IndexOf("}", appIndex);

            //Factory.Session.LogInfo($"appIndex {appIndex}");
            //Factory.Session.LogInfo($"targetIndex {targetIndex}");
            //Factory.Session.LogInfo($"platformIndex {platformIndex}");
            //Factory.Session.LogInfo($"configIndex {configIndex}");
            //Factory.Session.LogInfo($"close {close}");
            
            appStr = log.Substring(appIndex, targetIndex - appIndex);            
            targetStr = log.Substring(targetIndex, platformIndex - targetIndex);
            platformStr = log.Substring(platformIndex, configIndex - platformIndex);
            configStr = log.Substring(configIndex, close - configIndex);

            appStr = appStr.Replace("\"AppName\":", "");
            appStr = appStr.Replace("\"", "");
            appStr = appStr.Replace(",", "");

            targetStr = targetStr.Replace("\"TargetName\":", "");
            targetStr = targetStr.Replace("\"", "");
            targetStr = targetStr.Replace(",", "");

            platformStr = platformStr.Replace("\"Platform\":", "");
            platformStr = platformStr.Replace("\"", "");
            platformStr = platformStr.Replace(",", "");

            configStr = configStr.Replace("\"Configuration\":", "");
            configStr = configStr.Replace("\"", "");
            configStr = configStr.Replace(",", "");

            Factory.Session.LogInfo($"App Name {appStr}");
            Factory.Session.LogInfo($"Target Name {targetStr}");
            Factory.Session.LogInfo($"Platform {platformStr}");
            Factory.Session.LogInfo($"Configuration {configStr}");

            string typeCheck = targetStr.Replace(projectName, "");
            Factory.Session.LogInfo($"type check {typeCheck}");
            string[] typeNames = Enum.GetNames(typeof(TargetType));
            

            for (int i = 0; i < typeNames.Length; i++)
            {
                //Factory.Session.LogInfo($"type Name {typeNames[i]}");

                if (typeNames[i].Contains(typeCheck, StringComparison.OrdinalIgnoreCase))
                {
                    //Factory.Session.LogInfo("type match");
                    targetType = (TargetType)i;
                    break;
                }
            }

            string[] configNames = Enum.GetNames(typeof(UnrealTargetConfiguration));

            for (int i = 0; i < configNames.Length; i++)
            {
                //Factory.Session.LogInfo($"config Name {configNames[i]}");

                if (configNames[i].Contains(configStr, StringComparison.OrdinalIgnoreCase))
                {
                    //Factory.Session.LogInfo("config match");
                    targetConfig = (UnrealTargetConfiguration)i;
                    break;
                }
            }

            Factory.Session.LogInfo($"Target Type {targetType}");
            Factory.Session.LogInfo($"Config Type {targetConfig}");          
        }

        //============ START PLUGIN CHECKS ============
        List<PluginInfo> pluginList = Plugins.ReadAvailablePlugins(engineRef, projectRef, null);        
        Dictionary<string, ModuleToPlugin> pluginMap = new Dictionary<string, ModuleToPlugin>();
        HashSet<UHTManifest.Module> finalModules = new HashSet<UHTManifest.Module>();               
        HashSet<string> disabledModuleNames = new HashSet<string>();
              
        HashSet<UhtPackage> finalPackages = new HashSet<UhtPackage>();        
        FileReference projRef = FileReference.FromString(Factory.Session.ProjectFile!);
        FileReference targetFileRef = TargetReceipt.GetDefaultPath(projectRef, targetName, targetPlatform, targetConfig, config.ActiveArchitectures(projRef, targetName));
        Factory.Session.LogInfo($"Target path: {targetFileRef.ToString()}");
        
        TargetReceipt targetReceipt = TargetReceipt.Read(targetFileRef);

        if (targetReceipt == null)
        {
            Factory.Session.LogInfo("Target Receipt failed to load");
            return;
        }

        Dictionary<string, PluginInfo> allPlugins = new Dictionary<string, PluginInfo>();
        HashSet<string> codePluginNames = new HashSet<string>();

        foreach(var plug in pluginList)
        {
            allPlugins.Add(plug.Name, plug);
        }

        //Unfortunately, this function is private, but seems to be the only reliable way I've found
        //to properly check if plugins are enabled or not
        System.Reflection.BindingFlags flags = System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static;

        Type nativeProj = typeof(NativeProjects);
        System.Reflection.MethodInfo method = nativeProj.GetMethod("GetCodePluginsForTarget", flags)!;
        PluginReferenceDescriptor? OutMissingPlugin = null;


        if (method == null)
        {
            Factory.Session.LogInfo($"Failed to Find GetCodePluginsForTarget Method");
            return;
        }
        
        method.Invoke
        (
            null,
            new object[]
            {
                project,
                targetPlatform,
                targetConfig,
                targetType,
                codePluginNames,
                allPlugins,
                OutMissingPlugin,
                Log.Logger
            }
        );

        int moduleID = 0;

        foreach(var key in allPlugins.Keys)
        {
            if (!codePluginNames.Contains(key))
            {
                PluginInfo info = allPlugins[key];
                var modList = info.Descriptor.Modules!;
                if (modList == null) continue;

                //Factory.Session.LogInfo($"Disabled plugin {key}");
                foreach(var mod in modList)
                {
                    disabledModuleNames.Add(mod.Name);
                    //Factory.Session.LogInfo($"Disabled {mod.Name}");
                }
            }

            else
            {
                PluginInfo info = allPlugins[key];
                var modList = info.Descriptor.Modules!;
                if (modList == null) continue;

                foreach(var mod in modList)
                {
                    if (!pluginMap.ContainsKey(mod.Name))
                    {
                        pluginMap.Add(mod.Name, new ModuleToPlugin() { name = key, ID = moduleID });
                        moduleID++;
                    }                    
                }                
            }
        }

        Factory.Session.LogInfo($"Total Plugin Count {codePluginNames.Count}");
       
        foreach (var pack in Factory.Session.Packages)
        {
            if (disabledModuleNames.Contains(pack.Module.Name))            
                continue;
                        
            finalPackages.Add(pack);
        }
        
        HashSet<string> addedPlugins = new HashSet<string>();
        List<string> addedModuleNames = new List<string>();

        //============ START PACKAGE LOOP ============        
        foreach(UhtPackage package in finalPackages)
        {
            if (package.Module.Name.Contains(angelscript) || package.Module.BaseDirectory.Contains(editor))
            {                
                if (package.Module.Name.Equals("AngelscriptNativeBinds", StringComparison.OrdinalIgnoreCase))
                {
                    targetModule = package;
                    targetDir = package.Module.OutputDirectory;
                    if (debugMode)
                    {
                        targetDir = package.Module.BaseDirectory;
                        Factory.Session.LogInfo($"Debug Base Dir: {targetDir}");
                    }
                }

                rejectedModules++;
                continue;
            }

            if (skipList.moduleSkips.Contains(package.Module.Name))
            {
                Factory.Session.LogInfo($"Skipping Module: {package.Module.Name}");
                rejectedModules++;
                continue;
            }
           
            ModuleData data = new ModuleData(package);
            bool addedBinds = false;

            //============ START HEADER LOOP ============
            foreach (UhtType header in package.Children)
            {
                if (header is UhtHeaderFile == false) continue;

                UhtHeaderFile headerFile = (UhtHeaderFile)header;
                if (headerFile.HeaderFileType == UhtHeaderFileType.Private)
                    continue;

                StringBuilder builder = new StringBuilder();
                includeTypes.Clear();
                argIncludes.Clear();
                argIncludes.Add(coreMin);
                argIncludes.Add(asInclude);

                builder.Append(asInclude + '\n');
                argIncludes.Add(headerFile.IncludeFilePath);
                builder.Append(coreMin + '\n');                
                builder.Append($"#include \"{headerFile.IncludeFilePath}\"\n");
              
                //============ START TYPE LOOP ============
                foreach (UhtType type in headerFile.Children)
                {
                    if (type is UhtClass == false) continue;
                    UhtClass uhtClass = (UhtClass)type;
                    classSkip.className = uhtClass.EngineName;
                    funcSkip.className = uhtClass.EngineName;
                    
                    if (skipList.skips.Contains(classSkip))
                    {
                        Factory.Session.LogInfo($"Skipping class: {uhtClass.EngineName}");
                        rejectedTypes++;
                        continue;
                    }

                    if (uhtClass.Deprecated) continue;
                    if (uhtClass.ClassType == UhtClassType.Interface) continue;
                    if (uhtClass.ClassType == UhtClassType.NativeInterface) continue;

                    IList<UhtDeclaration> decls = uhtClass.Declarations!;
                    if (decls == null) continue;

                    SpanLineEnumerator lines = header.HeaderFile.Data.Span.EnumerateLines();
                    int classLineNum = uhtClass.LineNumber;

                    int classLineCount = 0;
                    bool hasDeprecated = false;
                    string classLine = "";

                    //LINE CHECKS
                    foreach (var line in lines)
                    {
                        if (classLineCount == classLineNum - 1)
                        {
                            if (line.Contains("UINTERFACE", StringComparison.OrdinalIgnoreCase))
                            {
                                Factory.Session.LogInfo($"Skip UInterface {uhtClass.SourceName}");
                                hasDeprecated = true;
                                rejectedTypes++;
                                break;
                            }
                        }

                        if (classLineCount == classLineNum)
                        {
                            classLine = line.ToString();
                            if (line.Contains(uhtClass.SourceName, StringComparison.OrdinalIgnoreCase))
                            {
                                if (line.Contains("UE_DEPRECATED", StringComparison.OrdinalIgnoreCase))
                                {
                                    Factory.Session.LogInfo($"Skip Deprecated class {uhtClass.SourceName}");
                                    hasDeprecated = true;
                                    rejectedTypes++;
                                    break;
                                }

                                if (line.Contains($"{uhtClass.SourceName}_Deprecated", StringComparison.OrdinalIgnoreCase))
                                {
                                    Factory.Session.LogInfo($"Skip Deprecated class 2 {uhtClass.SourceName}");
                                    hasDeprecated = true;
                                    rejectedTypes++;
                                    break;
                                }
                            }
                        }

                        classLineCount++;
                    }

                    if (hasDeprecated) continue;

                    bool addedEntries = false;
                    bool useSoftClassRegister = false;
                    bool classMinAPI = uhtClass.ClassFlags.HasAnyFlags(EClassFlags.MinimalAPI);
                    bool classReqAPI = uhtClass.ClassFlags.HasAnyFlags(EClassFlags.RequiredAPI);
                    
                    StringBuilder bind = new StringBuilder();
                    string fnDecl = $"void Bind_{uhtClass.SourceName}()";
                    bind.Append($"\nAS_FORCE_LINK {fnDecl}\n" + "{\n");

                    if (!classMinAPI && !classReqAPI)
                    {
                        useSoftClassRegister = true;
                        //Factory.Session.LogInfo($"Soft Class Registry: {uhtClass.SourceName}");
                    }

                    //============ START DECL LOOP ============
                    foreach (UhtDeclaration decl in decls)
                    {
                        UhtFunction function = decl.Function!;
                        if (function == null) continue;

                        funcSkip.functionName = function.SourceName;

                        if (skipList.skips.Contains(funcSkip))
                        {
                            Factory.Session.LogInfo($"Skipping Function {uhtClass.SourceName}::{function.SourceName}");
                            rejectedFuncs++;
                            continue;
                        }

                        if (!function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintCallable) || function.Deprecated)
                        {
                            rejectedFuncs++;
                            continue;
                        }
                        
                        if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.Private | EFunctionFlags.Protected | EFunctionFlags.EditorOnly))
                        {
                            rejectedFuncs++;
                            continue;
                        }

                        if (function.SourceName.Contains("Deprecated"))
                        {
                            rejectedFuncs++;
                            continue;
                        }
                        
                        int fnLineCount = 0;
                        int fnlineNum = function.LineNumber;
                        lines = header.HeaderFile.Data.Span.EnumerateLines();
                        bool isDeprecated = false;

                        foreach (var line in lines)
                        {
                            //The UE_DEPRECATED appears before UFUNCTION MACRO
                            if (fnLineCount == fnlineNum - 2) 
                            {
                                if (line.Contains("UE_DEPRECATED", StringComparison.OrdinalIgnoreCase))
                                {
                                    Factory.Session.LogInfo($"Deprecated fn skip {uhtClass.SourceName}::{function.SourceName}");
                                    isDeprecated = true;
                                    rejectedFuncs++;
                                    break;
                                }
                            }
                            fnLineCount++;
                        }

                        if (isDeprecated) continue;

                        StringBuilder str = new StringBuilder();
                        int len = decl.Tokens.Length;
                        bool hasSemicolon = false;
                        int endIndex = decl.Tokens.Length - 1;

                        //INITIAL END TOKEN SCAN
                        for (int i = 0; i < decl.Tokens.Length; i++)
                        {
                            if (decl.Tokens[i].IsValue(';'))
                            {
                                hasSemicolon = true;
                                endIndex = i;
                                break;
                            }

                            if (decl.Tokens[i].IsValue('{'))
                            {
                                hasSemicolon = false;
                                endIndex = i;
                                break;
                            }
                        }
                        
                        if (!classReqAPI && !function.FunctionFlags.HasAnyFlags(EFunctionFlags.RequiredAPI))
                        {
                            if (hasSemicolon)
                            {
                                rejectedFuncs++;
                                continue;
                            }
                        }

                        //BEGIN MAIN STRING SCANNING & CONSTRUCTION                       
                        int nameIndex = -1;
                        int parenStart = -1;
                        int parenEnd = endIndex;
                        int parenCounter = 0;

                        var args = function.ParameterProperties;
                        argNames.Clear();
                        fnTokens.Clear();
                        nestTypeSet.Clear();
                        bool hasNested = false;

                        //ADD RET AND PARAM INCLUDES
                        UhtProperty ret = function.ReturnProperty!;

                        if (ret != null)                        
                            AddParameterInclude(ret, includeTypes);

                        for (int i = 0; i < args.Span.Length; i++)
                        {
                            argNames.Add(args.Span[i].SourceName);
                            AddParameterInclude(args.Span[i], includeTypes);

                            if (args.Span[i] is UhtDelegateProperty del)
                            {
                                UhtType find = uhtClass.FindType(UhtFindOptions.SourceName | UhtFindOptions.DelegateFunction, del.GetUserFacingDecl(), null)!;

                                if (find != null && find.HeaderFile == uhtClass.HeaderFile)
                                {
                                    if (find.LineNumber > uhtClass.LineNumber)
                                    {
                                        hasNested = true;
                                        string argType = del.GetUserFacingDecl();
                                        //Factory.Session.LogInfo($"User Face Test: {uhtClass}::{argType.ToString()} del {find.HeaderFile} class {uhtClass.HeaderFile}");
                                        nestTypeSet.Add(argType.ToString());
                                    }
                                }
                            }
                        }

                        //PROCESS TOKENS
                        StringBuilder orig = new StringBuilder();
                        List<int> argNameIndices = new List<int>();

                        for (int i = 0; i < decl.Tokens.Length; i++)
                        {
                            var token = decl.Tokens[i];
                            if (token.IsValue("virtual")) continue;
                            if (token.IsValue("override")) continue;
                            if (token.IsValue("final")) continue;
                            if (token.IsValue("inline")) continue;
                            if (token.IsValue("FORCEINLINE")) continue;
                            if (token.IsValue("FORCENOINLINE")) continue;
                            if (token.IsValue("UPARAM")) continue;
                            
                            string tok = token.ToString();
                            if (tok.EndsWith("_API")) continue;

                            if (hasNested)
                            {
                                if (nestTypeSet.Contains(tok))
                                {
                                    orig.Append($"{uhtClass.SourceName}::");
                                    //Factory.Session.LogInfo($"Nested Insert: {uhtClass.SourceName}::{tok}");
                                }
                            }

                            orig.Append(tok);

                            if (i + 1 < decl.Tokens.Length)
                            {
                                var next = decl.Tokens[i + 1];
                                if (next.IsValue("::")) continue;
                                if (next.IsValue('<')) continue;
                                if (next.IsValue('>')) continue;
                                if (next.IsValue('&')) continue;
                                if (next.IsValue('*')) continue;
                                if (next.IsValue('(')) continue;
                            }

                            if (token.IsValue("::")) continue;
                            if (token.IsValue('<')) continue;
                            if (token.IsValue('(')) continue;

                            orig.Append(' ');
                        }                        

                        //FIND AND REMOVE ALL PARENTHESES RANGES
                        string view = orig.ToString();
                        nameIndex = view.IndexOf(function.SourceName);
                        parenStart = view.IndexOf('(', nameIndex);
                        int end = view.Length;

                        for (int i = parenStart; i < view.Length; i++)
                        {
                            if (view[i] == ';' || view[i] == '{')
                            {
                                end = i;
                                break;
                            }
                        }

                        if (end != view.Length)
                            view = view.Substring(0, end);

                        parenEnd = end;
                        int close = view.LastIndexOf(')');
                        if (close != -1) parenEnd = close;

                        List<Range> parenRanges = new List<Range>();
                        int start = 0;

                        for (int i = 0; i < view.Length; i++)
                        {
                            if (view[i] == '(' && i != parenStart)
                            {
                                if (parenCounter == 0) start = i;
                                parenCounter++;
                            }

                            if (view[i] == ')' && i != parenEnd)
                            {
                                parenCounter--;
                                if (parenCounter == 0)
                                    parenRanges.Add(new Range(start, i));

                                if (parenCounter < 0) parenCounter = 0;
                            }
                        }

                        List<string> subs = new List<string>();

                        foreach (Range range in parenRanges)                        
                            subs.Add(view.Substring(range.Start.Value, range.End.Value - range.Start.Value + 1));
                        
                        foreach (string s in subs)                        
                            view = view.Replace(s, "");
                        
                        //FIND ALL ARGUMENT NAMES AND DEFAULT VALUES
                        List<Range> argRanges = new List<Range>();

                        foreach (string name in argNames)
                        {
                            int argIndex = view.IndexOf(' ' + name + ' ', StringComparison.Ordinal);
                            if (argIndex < 0) continue;

                            int templateCounter = 0;

                            for (int i = argIndex; i < end; i++)
                            {
                                if (view[i] == '<')
                                    templateCounter++;

                                if (view[i] == '>')
                                    templateCounter--;

                                if (templateCounter == 0)
                                {
                                    if (view[i] == ',' || view[i] == ')')
                                    {
                                        argRanges.Add(new Range(argIndex, i));
                                        break;
                                    }
                                }
                            }
                        }

                        List<string> trueArgs = new List<string>();

                        foreach (Range range in argRanges)                        
                            trueArgs.Add(view.Substring(range.Start.Value, range.End.Value - range.Start.Value));

                        foreach (string arg in trueArgs)                        
                            view = view.Replace(arg, "");
                        
                        //WRITE FINAL ENTRY TO FILE
                        if (!function.FunctionFlags.HasAnyFlags(EFunctionFlags.Static))
                        {
                            if (!useSoftClassRegister)
                            {
                                str.Append($"\tFAngelscriptBinds::AddFunctionEntry({uhtClass.SourceName}");
                                str.Append($"::StaticClass(), \"{function.SourceName}\", ");
                            }
                            else
                            {
                                str.Append($"\tFAngelscriptBinds::AddFunctionEntry(\"{uhtClass.SourceName}\"");                                
                                str.Append($", \"{function.SourceName}\", ");
                            }

                            str.Append("{ ERASE_METHOD_PTR(" + $"{uhtClass.SourceName}, {function.SourceName}, ");
                        }

                        else
                        {
                            if (!useSoftClassRegister)
                            {
                                str.Append($"\tFAngelscriptBinds::AddFunctionEntry({uhtClass.SourceName}");
                                str.Append($"::StaticClass(), \"{function.SourceName}\", ");
                            }

                            else
                            {
                                str.Append($"\tFAngelscriptBinds::AddFunctionEntry(\"{uhtClass.SourceName}\"");                                
                                str.Append($", \"{function.SourceName}\", ");
                            }

                            str.Append("{ ERASE_FUNCTION_PTR(" + $"{uhtClass.SourceName}::{function.SourceName}, ");
                        }

                        int newParenStart = view.IndexOf('(');
                        int newNameIndex = view.IndexOf($" {function.SourceName}");

                        str.Append(view.Substring(newParenStart, view.Length - newParenStart));
                        StringBuilder retStr = new StringBuilder();

                        retStr.Append(view.Substring(0, newNameIndex));
                        str.Append($", ERASE_ARGUMENT_PACK({retStr.ToString()}))" + " } );");
                        bind.Append(str.ToString() + "\n");
                        addedEntries = true;
                        acceptedFuncs++;
                    } 
                    //============ END DECL LOOP ============

                    if (addedEntries)
                    {
                        addedBinds = true;
                        bind.Append("}\n");
                        data.funcDecls.Add(fnDecl);

                        //Process Additional Includes
                        foreach (var include in includeTypes)
                        {
                            if (include.HeaderFileType == UhtHeaderFileType.Private) continue;

                            if (!data.dependencies.Contains(include.Package) && include.Package != data.package)
                                data.dependencies.Add(include.Package);

                            if (!argIncludes.Contains(include.IncludeFilePath))                            
                            {
                                argIncludes.Add(include.IncludeFilePath);
                                builder.Append($"#include \"{include.IncludeFilePath}\"\n");                                
                            }

                            //if (!argIncludes.Contains(include.FilePath))
                            //{
                            //    argIncludes.Add(include.FilePath);                                
                            //    string argPath = Path.GetRelativePath(targetDir, include.FilePath).Replace("\\", "/");
                            //    builder.Append($"#include \"{argPath}\"\n");
                            //}

                            //foreach (var extra in include.IncludedHeaders)
                            //{
                            //    if (!data.dependencies.Contains(extra.Package) && extra.Package != data.package)
                            //        data.dependencies.Add(extra.Package);
                            //}
                        }

                        builder.Append(bind.ToString());
                        acceptedTypes++;
                    }
                }
                //============ END TYPES LOOP ============

                if (addedBinds)
                {
                    //This is a deliberate Organization trick
                    string fileName = "Bind_" + headerFile.FileNameWithoutExtension + ".gen.cpp"; //was .cpp

                    if (debugMode)
                        fileName = "Bind_" + headerFile.FileNameWithoutExtension + ".cpp";
                    
                    string file = builder.ToString();

                    if (file.Contains("AS_FORCE_LINK"))
                    {
                        data.fileNames.Add(fileName);
                        data.sourceFiles.Add(file);
                    }
                }

                else
                {
                    string fileName = "Bind_" + headerFile.FileNameWithoutExtension + ".gen.cpp"; //was cpp

                    if (debugMode)
                        fileName = "Bind_" + headerFile.FileNameWithoutExtension + ".cpp";

                    data.deleteFiles.Add(fileName);
                }

            } 
            //============ END HEADERS LOOP ============

            if (addedBinds)
            {
                //ADD MODULE DATA TO MAIN LIST HERE
                if (data.sourceFiles.Count > 0)                
                    modules.Add(data);

                addedModuleNames.Add(package.Module.Name);
                if (pluginMap.ContainsKey(package.Module.Name))
                {
                    if (!addedPlugins.Contains(pluginMap[package.Module.Name].name))
                        addedPlugins.Add(pluginMap[package.Module.Name].name);
                }

                else
                {
                    Factory.Session.LogInfo($"Entry not found for {package.Module.Name}");
                }
            }

            //DELETE EXISTING MODULES THAT NO LONGER HAVE BINDS, MAY BE NEEDED LATER
            //else
            //{
            //    if (data.deleteFiles.Count > 0 && data.sourceFiles.Count == 0)
            //    {
            //        modules.Add(data);
            //        Factory.Session.LogInfo($"Adding Delete Module {modules.Count - 1}");
            //        foreach (var file in data.deleteFiles)
            //            Factory.Session.LogInfo($"{modules.Count - 1}: {file}");
            //        Factory.Session.LogInfo(" ");
            //    }
            //}
        }
        //============ END PACKAGE LOOP ============

        //BEGIN FINAL MODULE WRITING
        //Need to clean this up later, a lot of these probably aren't needed now
        List<string> moduleNames = new List<string>();
        HashSet<UhtPackage> finalDepends = new HashSet<UhtPackage>();
        List<ModuleData> finalModuleList = new List<ModuleData>();
        List<string> privateDeps = new List<string>();        
        List<string> registerCalls = new List<string>();
        List<string> finalIncludes = new List<string>();
        HashSet<UhtPackage> plugins = new HashSet<UhtPackage>();
        HashSet<string> allModules = new HashSet<string>(); //The new one we use for all in the end
        int maxModules = 10;
        int count = 0;

        //START FINAL MODULE LOOP
        while (count < modules.Count)
        {
            for (int i = 0; i < maxModules; i++)
            {
                if (count >= modules.Count)
                    break;

                finalModuleList.Add(modules[count]);
                count++;
            }

            foreach (ModuleData module in finalModuleList)
            {
                if (!finalDepends.Contains(module.package))
                    finalDepends.Add(module.package);
            }

            foreach (ModuleData module in finalModuleList)
            {
                foreach (UhtPackage dep in module.dependencies)
                {
                    if (!finalDepends.Contains(dep))
                        finalDepends.Add(dep);
                }
            }

            int id = count - maxModules;
            string className = $"ASBind_{id}";

            foreach (var dep in finalDepends)
            {
                privateDeps.Add(dep.Module.Name);
                allModules.Add(dep.Module.Name);
            }

            //WRITE BUILD FILE
            //Factory.Session.LogInfo(buildPath);
            //Factory.CommitOutput(buildPath, build.ToString());

            /*
            Change 'Modules' to some generic header files to be included by
            One master header, which the native bind module should include and call
            the function(s)
            */

            StringBuilder moduleHeader = new StringBuilder();
            StringBuilder moduleCPP = new StringBuilder();
            CreateModuleFilesV2(className, moduleHeader, moduleCPP, finalModuleList, registerCalls);
            
            string headPath = $@"{targetDir}\{className}.h";
            string cppPath = $@"{targetDir}\{className}.gen.cpp";

            if (debugMode)
            {
                headPath = $@"{targetDir}\Private\{className}.h";
                cppPath = $@"{targetDir}\Private\{className}.cpp";
            }

            finalIncludes.Add($"#include \"{className}.h\"\n");            

            //MAIN MODULE PROCESSING LOOP
            foreach (var module in finalModuleList)
            {                
                for (int i = 0; i < module.sourceFiles.Count; i++)
                {
                    //WRITE BIND FILE
                    string path = $@"{targetDir}\{module.fileNames[i]}";                    

                    if (debugMode)
                        path = $@"{targetDir}\Private\{module.fileNames[i]}";

                    Factory.CommitOutput(path, module.sourceFiles[i].Insert(0, $"#include \"{className}.h\"\n"));

                    if (module.package.Package.IsPlugin)
                        plugins.Add(module.package.Package);                    
                }

                for (int i = 0; i < module.deleteFiles.Count; i++)
                {
                    //DELETE OLD FILE                    
                    string path = $@"{targetDir}\{module.deleteFiles[i]}";

                    if (debugMode)
                        path = $@"{targetDir}\Private\{module.deleteFiles[i]}";

                    if (Directory.Exists(path)) File.Delete(path);
                }

                for (int i = 0; i < module.funcDecls.Count; i++)                
                    moduleHeader.Append($"{module.funcDecls[i]};\n");
                
                for (int i = 0; i < module.noAPIDebugs.Count; i++)                
                    Factory.Session.LogInfo($"{className} NO_API: {module.noAPIDebugs[i]}");
            }

            //WRITE MODULE HEADER AND CPP
            Factory.CommitOutput(headPath, moduleHeader.ToString());
            Factory.CommitOutput(cppPath, moduleCPP.ToString());

            moduleNames.Add(className);
            //Factory.AddExternalDependency(headPath);

            finalModuleList.Clear();
            finalDepends.Clear();
            privateDeps.Clear();
        }

        //UPDATE NATIVE BINDS BUILD FILE
        string nativeBind = "AngelscriptNativeBinds";
        string bindFile = "AngelscriptNativeBinds.Build.cs";
        string bindPath = Factory.Session.ProjectDirectory + sourceDir + $@"{nativeBind}\{bindFile}";        
        
        StringBuilder native = CreateModuleBuildFile(nativeBind, publicDeps, allModules.ToList<string>());
        Factory.CommitOutput(bindPath, native);

        StringBuilder finalHeader = new StringBuilder();        
        finalHeader.Append("#pragma once\n");
        finalHeader.Append("#include \"AngelscriptBinds.h\"\n\n");
        //finalHeader.Append(asInclude + "\n\n");
        //finalHeader.Append(asInclude + "\n\n");
        //finalHeader.Append("void Bind_Angelscript();");

        foreach (string inc in finalIncludes)
        {
            finalHeader.Append(inc);
        }

        finalHeader.Append('\n');
        finalHeader.Append("void Bind_Angelscript()\n{\n");
        finalHeader.Append($"\tFAngelscriptBinds::RegisterBinds\n\t(\n");
        finalHeader.Append("\t\t(int32)FAngelscriptBinds::EOrder::Late, []()\n");
        finalHeader.Append("\t\t{\n");

        foreach (string reg in registerCalls)
        {
            finalHeader.Append($"\t\t\t{reg}\n");
        }

        finalHeader.Append("\t\t}\n");
        finalHeader.Append("\t);\n");
        finalHeader.Append("}\n");

        string finalHeadPath = $@"{targetDir}\AngelscriptRegister.h";

        if (debugMode)
            finalHeadPath = $@"{targetDir}\Private\AngelscriptRegister.h";        

        Factory.CommitOutput(finalHeadPath, finalHeader);        

        string pluginPath = Factory.Session.ProjectDirectory + @"\Plugins\Angelscript\Angelscript.uplugin";
        Factory.Session.LogInfo($"Plugin Path: {pluginPath}");
        FileReference pluginRef = new FileReference(pluginPath);
        PluginDescriptor pluginDesc = PluginDescriptor.FromFile(pluginRef);

        //if (pluginDesc.Plugins != null && pluginDesc.Plugins.Count > 0)
        if (pluginDesc.Plugins == null)
            pluginDesc.Plugins = new List<PluginReferenceDescriptor>();
        else
            pluginDesc.Plugins.Clear();

        foreach(var plugin in addedPlugins)
        {
            pluginDesc.Plugins.Add(new PluginReferenceDescriptor(plugin, "", true));
        }

        pluginDesc.Save(pluginPath);

        Factory.Session.LogInfo($"Total modules {modules.Count}");
        Factory.Session.LogInfo($"Rejected modules {rejectedModules}");
        Factory.Session.LogInfo($"Accepted Types: {acceptedTypes} Rejected Types {rejectedTypes}");
        Factory.Session.LogInfo($"Accepted Funcs: {acceptedFuncs} Rejected Funcs {rejectedFuncs}");
        
        foreach(var add in addedPlugins)
        {
            Factory.Session.LogInfo($"Added Plugin: {add}");
        }

        foreach(string mod in addedModuleNames)
        {
            Factory.Session.LogInfo($"Bound module {mod}");
        }

        Factory.Session.LogInfo($"Total bound names {addedModuleNames.Count}");

        //Factory.Session.Config                            
        //Factory.Session.Manifest.Modules
        //Factory.Session.ManifestFile.
        //Factory.Session.ProjectDirectory

        /*
        The Exhaustive finalModuleList of all modules is stored in Binaries under the Target file
        In the main project file is of class FProjectDescriptor,
        this will list any plugins that you went out of your way in its plugins finalModuleList
        to disable, as well as have a bool named DisableEnginePlugins so that plugins are only
        added explicitly.
        */
               
        /*
        It is possible that we are getting the massive number of plugins via
        the uproject folder containing dependencies and then automatically add all of their dependencies
        It seems some plugins are just enabled by default even deep in the engine
        So we could just scan the uproject file, and resolve some of them.
        */
    }

    public static void CreateModuleFilesV2(string className, StringBuilder header, StringBuilder cpp, List<ModuleData> modList, List<string> registerCalls)
    {
        header.Append("#pragma once\n");
        header.Append(asInclude + "\n\n");
        //header.Append("#include \"CoreMinimal.h\"\n");
        //header.Append("#include \"Modules/ModuleManager.h\"\n");
        header.Append($"void Register_{className}();\n\n");
        registerCalls.Add($"Register_{className}();");

        cpp.Append($"#include \"{className}.h\"\n");
        cpp.Append(asInclude + "\n\n");

        cpp.Append($"void Register_{className}()");
        cpp.Append("\n{\n");

        //cpp.Append($"\tFAngelscriptBinds::RegisterBinds\n\t(\n");
        //cpp.Append("\t\t(int32)FAngelscriptBinds::EOrder::Late, []()\n");
        //cpp.Append("\t\t{\n");

        foreach (ModuleData mod in modList)
        {
            foreach (string fnDecl in mod.funcDecls)
            {
                cpp.Append($"\t{fnDecl.Substring(4)};\n");
                //cpp.Append($"\t\t\t{fnDecl.Substring(4)};\n");
            }
        }

        //cpp.Append("\t\t}\n");
        //cpp.Append("\t);\n");
        cpp.Append("}\n");
    }

    public static void CreateModuleFiles(string className, StringBuilder header, StringBuilder cpp, List<ModuleData> modList)
    {
        header.Append("#pragma once\n");
        header.Append("#include \"CoreMinimal.h\"\n");
        header.Append("#include \"Modules/ModuleManager.h\"\n");            

        header.Append($"class {className.ToUpper()}_API F{className}Module : public FDefaultModuleImpl\n");            

        header.Append("{\n");
        header.Append("public:\n");
        header.Append("\tvirtual void StartupModule() override;\n");
        header.Append("\tvirtual void ShutdownModule() override;\n");
        header.Append("};\n");

        cpp.Append($"#include \"{className}Module.h\"\n");
        cpp.Append("#include \"AngelscriptBinds.h\"\n");
        
        string ModuleClass = $"F{className}Module";
        cpp.Append($"IMPLEMENT_MODULE({ModuleClass}, {className});\n\n");            

        cpp.Append($"void {ModuleClass}::StartupModule()");
        cpp.Append("\n{\n");

        cpp.Append($"\tFAngelscriptBinds::RegisterBinds\n\t(\n");
        cpp.Append("\t\t(int32)FAngelscriptBinds::EOrder::Late, []()\n");
        cpp.Append("\t\t{\n");

        foreach(ModuleData mod in modList)
        {
            foreach(string fnDecl in mod.funcDecls)
            {
                cpp.Append($"\t\t\t{fnDecl.Substring(4)};\n");
            }
        }

        cpp.Append("\t\t}\n");
        cpp.Append("\t);\n");
        cpp.Append("}\n");

        cpp.Append($"void {ModuleClass}::ShutdownModule()");
        cpp.Append("{}\n\n");
    }

    public static StringBuilder CreateModuleBuildFile(string className, List<string> publicDeps, List<string> privateDeps)
    {            
        StringBuilder build = new StringBuilder();

        build.Append("using System.IO;\n");
        build.Append("using UnrealBuildTool;\n\n");

        build.Append("namespace UnrealBuildTool.Rules\n{\n");

        build.Append($"\tpublic class {className} : ModuleRules\n");
        build.Append("\t{\n");
        build.Append($"\t\tpublic {className}(ReadOnlyTargetRules Target) : base(Target)\n");
        build.Append("\t\t{\n");

        build.Append("\t\t\tPCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;\n");
        build.Append("\t\t\tOptimizeCode = CodeOptimization.Never;\n\n");
        build.Append("\t\t\tPublicDependencyModuleNames.AddRange\n\t\t\t(\n");

        build.Append("\t\t\t\tnew string[]\n");
        build.Append("\t\t\t\t{\n");

        foreach (string str in publicDeps)
        {
            build.Append($"\t\t\t\t\t\"{str}\",\n");
        }

        build.Append("\t\t\t\t}\n");
        build.Append("\t\t\t);\n"); //End Public Dependencies;

        build.Append("\t\t\tPrivateDependencyModuleNames.AddRange\n\t\t\t(\n");

        build.Append("\t\t\t\tnew string[]\n");
        build.Append("\t\t\t\t{\n");

        foreach (var dep in privateDeps)
        {
            build.Append($"\t\t\t\t\t\"{dep}\",\n");
        }

        build.Append("\t\t\t\t}\n");
        build.Append("\t\t\t);\n"); //End Private Dependencies;

        build.Append("\t\t}\n"); //End Constructor
        build.Append("\t}\n"); //End Class
        build.Append("}\n"); //End Namespace

        return build;
    }
    
    public static bool AddParameterInclude(UhtType type, HashSet<UhtHeaderFile> includeSet)
    {
        if (includeSet == null) return false;

        if (type is UhtArrayProperty arrayArg)
        {               
            AddParameterInclude(arrayArg.ValueProperty, includeSet);
        }

        if (type is UhtSetProperty setArg)
        {
            AddParameterInclude(setArg.ValueProperty, includeSet);
        }

        if (type is UhtMapProperty mapArg)
        {
            AddParameterInclude(mapArg.KeyProperty, includeSet);
            AddParameterInclude(mapArg.ValueProperty, includeSet);
        }

        if (type is UhtObjectProperty propertyArg)
        {           
            if (!includeSet.Contains(propertyArg.Class.HeaderFile))
                includeSet.Add(propertyArg.Class.HeaderFile);

            return true;
        }

        if (type is UhtClassProperty classArg)
        {          
            if (!includeSet.Contains(classArg.Class.HeaderFile))
                includeSet.Add(classArg.Class.HeaderFile);

            return true;
        }

        if (type is UhtStructProperty structArg)
        {            
            if (!includeSet.Contains(structArg.ScriptStruct.HeaderFile))
                includeSet.Add(structArg.ScriptStruct.HeaderFile);

            return true;
        }

        if (type is UhtEnumProperty enumArg)
        {            
            if (!includeSet.Contains(enumArg.Enum.HeaderFile))
                includeSet.Add(enumArg.Enum.HeaderFile);

            return true;
        }

        if (type is UhtDelegateProperty delegateArg)
        {            
            if (!includeSet.Contains(delegateArg.HeaderFile))
                includeSet.Add(delegateArg.HeaderFile);

            return true;
        }

        return false;
    }
}   
