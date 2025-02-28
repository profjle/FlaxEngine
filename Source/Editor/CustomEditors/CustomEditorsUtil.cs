// Copyright (c) 2012-2023 Wojciech Figat. All rights reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using FlaxEditor.CustomEditors.Editors;
using FlaxEditor.Scripting;
using FlaxEngine;

namespace FlaxEditor.CustomEditors
{
    internal static class CustomEditorsUtil
    {
        internal static readonly Dictionary<Type, string> InBuildTypeNames = new Dictionary<Type, string>()
        {
            { typeof(bool), "bool" },
            { typeof(byte), "byte" },
            { typeof(sbyte), "sbyte" },
            { typeof(char), "char" },
            { typeof(short), "short" },
            { typeof(ushort), "ushort" },
            { typeof(int), "int" },
            { typeof(uint), "uint" },
            { typeof(long), "ulong" },
            { typeof(float), "float" },
            { typeof(double), "double" },
            { typeof(decimal), "decimal" },
            { typeof(string), "string" },
        };

        /// <summary>
        /// Gets the type name for UI. Handles in-build types like System.Single and returns float.
        /// </summary>
        /// <param name="type">The type.</param>
        /// <returns>The result.</returns>
        public static string GetTypeNameUI(Type type)
        {
            if (!InBuildTypeNames.TryGetValue(type, out var result))
            {
                result = type.Name;
            }
            return result;
        }

        internal static CustomEditor CreateEditor(ValueContainer values, CustomEditor overrideEditor, bool canUseRefPicker = true)
        {
            // Check if use provided editor
            if (overrideEditor != null)
                return overrideEditor;

            // Special case if property is a pure object type and all values are the same type
            if (values.Type.Type == typeof(object) && values.Count > 0 && values[0] != null && !values.HasDifferentTypes)
                return CreateEditor(TypeUtils.GetObjectType(values[0]), canUseRefPicker);

            // Use editor for the property type
            return CreateEditor(values.Type, canUseRefPicker);
        }

        internal static CustomEditor CreateEditor(ScriptType targetType, bool canUseRefPicker = true)
        {
            if (targetType == ScriptType.Null)
                return new GenericEditor();
            if (targetType.IsArray)
            {
                if (targetType.Type.GetArrayRank() != 1)
                    return new GenericEditor(); // Not-supported multidimensional array
                return new ArrayEditor();
            }
            var targetTypeType = TypeUtils.GetType(targetType);
            if (canUseRefPicker)
            {
                if (typeof(Asset).IsAssignableFrom(targetTypeType))
                {
                    return new AssetRefEditor();
                }
                if (typeof(FlaxEngine.Object).IsAssignableFrom(targetTypeType))
                {
                    return new FlaxObjectRefEditor();
                }
            }

            // Use custom editor
            {
                var checkType = targetTypeType;
                do
                {
                    var type = Internal_GetCustomEditor(checkType);
                    if (type != null)
                    {
                        return (CustomEditor)Activator.CreateInstance(type);
                    }
                    checkType = checkType.BaseType;

                    // Skip if cannot use ref editors
                    if (!canUseRefPicker && checkType == typeof(FlaxEngine.Object))
                        break;
                } while (checkType != null);
            }

            // Use attribute editor
            var attributes = targetType.GetAttributes(false);
            var customEditorAttribute = (CustomEditorAttribute)attributes.FirstOrDefault(x => x is CustomEditorAttribute);
            if (customEditorAttribute != null)
                return (CustomEditor)Activator.CreateInstance(customEditorAttribute.Type);

            // Select default editor (based on type)
            if (targetType.IsEnum)
            {
                return new EnumEditor();
            }
            if (targetType.IsGenericType)
            {
                if (targetTypeType.GetGenericTypeDefinition() == typeof(Dictionary<,>))
                {
                    return new DictionaryEditor();
                }

                // Use custom editor
                var genericTypeDefinition = targetType.GetGenericTypeDefinition();
                var type = Internal_GetCustomEditor(genericTypeDefinition);
                if (type != null)
                {
                    return (CustomEditor)Activator.CreateInstance(type);
                }
            }

            // The most generic editor
            return new GenericEditor();
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        internal static extern Type Internal_GetCustomEditor(Type targetType);
    }
}
