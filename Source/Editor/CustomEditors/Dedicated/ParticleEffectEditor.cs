// Copyright (c) 2012-2023 Wojciech Figat. All rights reserved.

using System.Linq;
using FlaxEditor.Surface;
using FlaxEngine;

namespace FlaxEditor.CustomEditors.Dedicated
{
    /// <summary>
    /// Custom editor for <see cref="ParticleEffect"/>.
    /// </summary>
    /// <seealso cref="ActorEditor" />
    [CustomEditor(typeof(ParticleEffect)), DefaultEditor]
    public class ParticleEffectEditor : ActorEditor
    {
        private bool _isValid;
        private bool _isActive;
        private uint _parametersVersion;

        private bool IsValid
        {
            get
            {
                // All selected particle effects use the same system
                var effect = (ParticleEffect)Values[0];
                var system = effect.ParticleSystem;
                return system != null && Values.TrueForAll(x => (x as ParticleEffect)?.ParticleSystem == system);
            }
        }

        private object ParameterGet(object instance, GraphParameter parameter, object tag)
        {
            if (instance is ParticleEffect particleEffect && particleEffect && parameter && tag is ParticleEffectParameter effectParameter && effectParameter)
                return particleEffect.GetParameterValue(effectParameter.TrackName, parameter.Name);
            return null;
        }

        private void ParameterSet(object instance, object value, GraphParameter parameter, object tag)
        {
            if (instance is ParticleEffect particleEffect && particleEffect && parameter && tag is ParticleEffectParameter effectParameter && effectParameter)
                particleEffect.SetParameterValue(effectParameter.TrackName, parameter.Name, value);
        }

        private object ParameterDefaultValue(object instance, GraphParameter parameter, object tag)
        {
            if (tag is ParticleEffectParameter effectParameter)
                return effectParameter.DefaultValue;
            return null;
        }

        /// <inheritdoc />
        public override void Initialize(LayoutElementsContainer layout)
        {
            base.Initialize(layout);

            _isValid = IsValid;
            if (!_isValid)
                return;
            var effect = (ParticleEffect)Values[0];
            _parametersVersion = effect.ParametersVersion;
            _isActive = effect.IsActive;

            // Show all effect parameters grouped by the emitter track name
            var groups = layout.Group("Parameters");
            groups.Panel.Open();
            var parameters = effect.Parameters;
            var parametersGroups = parameters.GroupBy(x => x.EmitterIndex);
            foreach (var parametersGroup in parametersGroups)
            {
                var trackName = parametersGroup.First().TrackName;
                var group = groups.Group(trackName);
                group.Panel.Open();

                var data = SurfaceUtils.InitGraphParameters(parametersGroup);
                SurfaceUtils.DisplayGraphParameters(group, data, ParameterGet, ParameterSet, Values, ParameterDefaultValue);
            }
        }

        /// <inheritdoc />
        internal override void RefreshRootChild()
        {
            var effect = (ParticleEffect)Values[0];
            if (effect == null)
            {
                base.RefreshRootChild();
                return;
            }

            // Custom refreshing that handles particle effect parameters list editing during refresh (eg. effect gets disabled)
            if (_isValid != IsValid)
            {
                RebuildLayout();
                return;
            }
            Refresh();
            var parameters = effect.Parameters;
            if (parameters.Length == 0)
            {
                base.RefreshRootChild();
                return;
            }
            
            for (int i = 0; i < ChildrenEditors.Count; i++)
            {
                if (_isActive != effect.IsActive || _parametersVersion != effect.ParametersVersion)
                {
                    RebuildLayout();
                    return;
                }
                ChildrenEditors[i].RefreshInternal();
            }
        }
    }
}
