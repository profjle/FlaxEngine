// Copyright (c) 2012-2023 Wojciech Figat. All rights reserved.

using System;
using System.Globalization;
using System.IO;
using System.Text;
using FlaxEngine.GUI;
using Newtonsoft.Json;

namespace FlaxEngine
{
    /// <summary>
    /// The canvas rendering modes.
    /// </summary>
    public enum CanvasRenderMode
    {
        /// <summary>
        /// The screen space rendering mode that places UI elements on the screen rendered on top of the scene. If the screen is resized or changes resolution, the Canvas will automatically change size to match this.
        /// </summary>
        ScreenSpace = 0,

        /// <summary>
        /// The camera space rendering mode that places Canvas in a given distance in front of a specified Camera. The UI elements are rendered by this camera, which means that the Camera settings affect the appearance of the UI. If the Camera is set to Perspective, the UI elements will be rendered with perspective, and the amount of perspective distortion can be controlled by the Camera Field of View. If the screen is resized, changes resolution, or the camera frustum changes, the Canvas will automatically change size to match as well.
        /// </summary>
        CameraSpace = 1,

        /// <summary>
        /// The world space rendering mode that places Canvas as any other object in the scene. The size of the Canvas can be set manually using its Transform, and UI elements will render in front of or behind other objects in the scene based on 3D placement. This is useful for UIs that are meant to be a part of the world. This is also known as a 'diegetic interface'.
        /// </summary>
        WorldSpace = 2,

        /// <summary>
        /// The world space rendering mode that places Canvas as any other object in the scene and orients it to face the camera. The size of the Canvas can be set manually using its Transform, and UI elements will render in front of or behind other objects in the scene based on 3D placement. This is useful for UIs that are meant to be a part of the world. This is also known as a 'diegetic interface'.
        /// </summary>
        WorldSpaceFaceCamera = 3,
    }

    /// <summary>
    /// PostFx used to render the <see cref="UICanvas"/>. Used when render mode is <see cref="CanvasRenderMode.CameraSpace"/> or <see cref="CanvasRenderMode.WorldSpace"/>.
    /// </summary>
    /// <seealso cref="FlaxEngine.PostProcessEffect" />
    [HideInEditor]
    public sealed class CanvasRenderer : PostProcessEffect
    {
        /// <summary>
        /// The canvas to render.
        /// </summary>
        public UICanvas Canvas;

        /// <inheritdoc />
        public override bool UseSingleTarget => true;

        /// <inheritdoc />
        public override PostProcessEffectLocation Location => Canvas.RenderLocation;

        /// <inheritdoc />
        public override int Order => Canvas.Order;

        /// <inheritdoc />
        public override void Render(GPUContext context, ref RenderContext renderContext, GPUTexture input, GPUTexture output)
        {
            var bounds = Canvas.Bounds;
            bounds.Transformation.Translation -= renderContext.View.Origin;
            if (renderContext.View.Frustum.Contains(bounds.GetBoundingBox()) == ContainmentType.Disjoint)
                return;

            Profiler.BeginEventGPU("UI Canvas");

            // Calculate rendering matrix (world*view*projection)
            Canvas.GetWorldMatrix(renderContext.View.Origin, out Matrix worldMatrix);
            Matrix.Multiply(ref worldMatrix, ref renderContext.View.View, out Matrix viewMatrix);
            Matrix.Multiply(ref viewMatrix, ref renderContext.View.Projection, out Matrix viewProjectionMatrix);

            // Pick a depth buffer
            GPUTexture depthBuffer = Canvas.IgnoreDepth ? null : renderContext.Buffers.DepthBuffer;

            // Render GUI in 3D
            var features = Render2D.Features;
            if (Canvas.RenderMode == CanvasRenderMode.WorldSpace || Canvas.RenderMode == CanvasRenderMode.WorldSpaceFaceCamera)
                Render2D.Features &= ~Render2D.RenderingFeatures.VertexSnapping;
            Render2D.CallDrawing(Canvas.GUI, context, input, depthBuffer, ref viewProjectionMatrix);
            Render2D.Features = features;

            Profiler.EndEventGPU();
        }
    }

    partial class UICanvas
    {
        private CanvasRenderMode _renderMode;
        private readonly CanvasRootControl _guiRoot;
        private CanvasRenderer _renderer;
        private bool _isLoading, _isRegisteredForTick;

        /// <summary>
        /// Gets or sets the canvas rendering mode.
        /// </summary>
        [EditorOrder(10), EditorDisplay("Canvas"), Tooltip("Canvas rendering mode.")]
        public CanvasRenderMode RenderMode
        {
            get => _renderMode;
            set
            {
                if (_renderMode != value)
                {
                    var previous = _renderMode;

                    _renderMode = value;

                    Setup();

                    // Reset size
                    if (previous == CanvasRenderMode.ScreenSpace || (_renderMode == CanvasRenderMode.WorldSpace || _renderMode == CanvasRenderMode.WorldSpaceFaceCamera))
                        Size = new Float2(500, 500);
                }
            }
        }

        /// <summary>
        /// Gets or sets the canvas rendering location within rendering pipeline. Used only in <see cref="CanvasRenderMode.CameraSpace"/> or <see cref="CanvasRenderMode.WorldSpace"/> or <see cref="CanvasRenderMode.WorldSpaceFaceCamera"/>.
        /// </summary>
        [EditorOrder(13), EditorDisplay("Canvas"), VisibleIf("Editor_Is3D"), Tooltip("Canvas rendering location within the rendering pipeline. Change this if you want GUI to affect the lighting or post processing effects like bloom.")]
        public PostProcessEffectLocation RenderLocation { get; set; } = PostProcessEffectLocation.Default;

        private int _order;

        /// <summary>
        /// Gets or sets the canvas rendering and input events gather order. Created GUI canvas objects are sorted before rendering (from the lowest order to the highest order). Canvas with the highest order can handle input event first.
        /// </summary>
        [EditorOrder(14), EditorDisplay("Canvas"), Tooltip("The canvas rendering and input events gather order. Created GUI canvas objects are sorted before rendering (from the lowest order to the highest order). Canvas with the highest order can handle input event first.")]
        public int Order
        {
            get => _order;
            set
            {
                if (_order != value)
                {
                    _order = value;
                    RootControl.CanvasRoot.SortCanvases();
                }
            }
        }

        /// <summary>
        /// Gets or sets a value indicating whether canvas can receive the input events.
        /// </summary>
        [EditorOrder(15), EditorDisplay("Canvas"), Tooltip("If checked, canvas can receive the input events.")]
        public bool ReceivesEvents { get; set; } = true;

#if FLAX_EDITOR
        private bool Editor_Is3D => _renderMode != CanvasRenderMode.ScreenSpace;

        private bool Editor_IsWorldSpace => _renderMode == CanvasRenderMode.WorldSpace || _renderMode == CanvasRenderMode.WorldSpaceFaceCamera;

        private bool Editor_IsCameraSpace => _renderMode == CanvasRenderMode.CameraSpace;

        private bool Editor_UseRenderCamera => _renderMode == CanvasRenderMode.CameraSpace || _renderMode == CanvasRenderMode.WorldSpaceFaceCamera;
#endif

        /// <summary>
        /// Gets or sets the size of the canvas. Used only in <see cref="CanvasRenderMode.WorldSpace"/> or <see cref="CanvasRenderMode.WorldSpaceFaceCamera"/>.
        /// </summary>
        [EditorOrder(20), EditorDisplay("Canvas"), VisibleIf("Editor_IsWorldSpace"), Tooltip("Canvas size.")]
        public Float2 Size
        {
            get => _guiRoot.Size;
            set
            {
                if (_renderMode == CanvasRenderMode.WorldSpace || _renderMode == CanvasRenderMode.WorldSpaceFaceCamera || _isLoading)
                {
                    _guiRoot.Size = value;
                }
            }
        }

        /// <summary>
        /// Gets or sets a value indicating whether ignore scene depth when rendering the GUI (scene objects won't cover the interface).
        /// </summary>
        [EditorOrder(30), EditorDisplay("Canvas"), VisibleIf("Editor_Is3D"), Tooltip("If checked, scene depth will be ignored when rendering the GUI (scene objects won't cover the interface).")]
        public bool IgnoreDepth { get; set; } = false;

        /// <summary>
        /// Gets or sets the camera used to place the GUI when render mode is set to <see cref="CanvasRenderMode.CameraSpace"/> or <see cref="CanvasRenderMode.WorldSpaceFaceCamera"/>.
        /// </summary>
        [EditorOrder(50), EditorDisplay("Canvas"), VisibleIf("Editor_UseRenderCamera"), Tooltip("Camera used to place the GUI when RenderMode is set to CameraSpace or WorldSpaceFaceCamera.")]
        public Camera RenderCamera { get; set; }

        /// <summary>
        /// Gets or sets the distance from the <see cref="RenderCamera"/> to place the plane with GUI. If the screen is resized, changes resolution, or the camera frustum changes, the Canvas will automatically change size to match as well.
        /// </summary>
        [EditorOrder(60), Limit(0.01f), EditorDisplay("Canvas"), VisibleIf("Editor_IsCameraSpace"), Tooltip("Distance from the RenderCamera to place the plane with GUI. If the screen is resized, changes resolution, or the camera frustum changes, the Canvas will automatically change size to match as well.")]
        public float Distance { get; set; } = 500;

        /// <summary>
        /// Gets the canvas GUI root control.
        /// </summary>
        public CanvasRootControl GUI => _guiRoot;

        /// <summary>
        /// Delegate schema for the callback used to perform custom canvas intersection test. Can be used to implement a canvas that has a holes or non-rectangular shape.
        /// </summary>
        /// <param name="location">The location of the point to test in coordinates of the canvas root control (see <see cref="GUI"/>).</param>
        /// <returns>True if canvas was hit, otherwise false.</returns>
        public delegate bool TestCanvasIntersectionDelegate(ref Float2 location);

        /// <summary>
        /// The callback used to perform custom canvas intersection test. Can be used to implement a canvas that has a holes or non-rectangular shape.
        /// </summary>
        [HideInEditor]
        public TestCanvasIntersectionDelegate TestCanvasIntersection;

        /// <summary>
        /// Delegate schema for callback used to evaluate the world-space ray from the screen-space position (eg. project mouse position).
        /// </summary>
        /// <param name="location">The location in screen-space.</param>
        /// <param name="ray">The output ray in world-space.</param>
        public delegate void CalculateRayDelegate(ref Float2 location, out Ray ray);

        /// <summary>
        /// The current implementation of the <see cref="CalculateRayDelegate"/> used to calculate the mouse ray in 3D from the 2D location. Cannot be null.
        /// </summary>
        public static CalculateRayDelegate CalculateRay = DefaultCalculateRay;

        /// <summary>
        /// The default implementation of the <see cref="CalculateRayDelegate"/> that uses the <see cref="Camera.MainCamera"/> to evaluate the 3D ray.
        /// </summary>
        /// <param name="location">The location in screen-space.</param>
        /// <param name="ray">The output ray in world-space.</param>
        public static void DefaultCalculateRay(ref Float2 location, out Ray ray)
        {
            var camera = Camera.MainCamera;
            if (camera)
            {
                ray = camera.ConvertMouseToRay(location * Platform.DpiScale);
            }
            else
            {
                ray = new Ray(Vector3.Zero, Vector3.Forward);
            }
        }

        #region Navigation

        /// <summary>
        /// The delay (in seconds) before a navigation input event starts repeating if input control is held down (Input Action mode is set to Pressing).
        /// </summary>
        [EditorOrder(505), EditorDisplay("Navigation", "Input Repeat Delay")]
        [Tooltip("TheThe delay (in seconds) before a navigation input event starts repeating if input control is held down (Input Action mode is set to Pressing).")]
        public float NavigationInputRepeatDelay { get; set; } = 0.5f;

        /// <summary>
        /// The delay (in seconds) between successive repeated navigation input events after the first one.
        /// </summary>
        [EditorOrder(506), EditorDisplay("Navigation", "Input Repeat Rate")]
        [Tooltip("The delay (in seconds) between successive repeated navigation input events after the first one.")]
        public float NavigationInputRepeatRate { get; set; } = 0.05f;

        /// <summary>
        /// The name of the input action for performing UI navigation Up (from Input Settings).
        /// </summary>
        [EditorOrder(510), EditorDisplay("Navigation", "Navigate Up")]
        [Tooltip("The name of the input action for performing UI navigation Up (from Input Settings).")]
        public string NavigationInputActionUp { get; set; } = "NavigateUp";

        /// <summary>
        /// The name of the input action for performing UI navigation Down (from Input Settings).
        /// </summary>
        [EditorOrder(520), EditorDisplay("Navigation", "Navigate Down")]
        [Tooltip("The name of the input action for performing UI navigation Down (from Input Settings).")]
        public string NavigationInputActionDown { get; set; } = "NavigateDown";

        /// <summary>
        /// The name of the input action for performing UI navigation Left (from Input Settings).
        /// </summary>
        [EditorOrder(530), EditorDisplay("Navigation", "Navigate Left")]
        [Tooltip("The name of the input action for performing UI navigation Left (from Input Settings).")]
        public string NavigationInputActionLeft { get; set; } = "NavigateLeft";

        /// <summary>
        /// The name of the input action for performing UI navigation Right (from Input Settings).
        /// </summary>
        [EditorOrder(540), EditorDisplay("Navigation", "Navigate Right")]
        [Tooltip("The name of the input action for performing UI navigation Right (from Input Settings).")]
        public string NavigationInputActionRight { get; set; } = "NavigateRight";

        /// <summary>
        /// The name of the input action for performing UI navigation Submit (from Input Settings).
        /// </summary>
        [EditorOrder(540), EditorDisplay("Navigation", "Navigate Submit")]
        [Tooltip("The name of the input action for performing UI navigation Submit (from Input Settings).")]
        public string NavigationInputActionSubmit { get; set; } = "NavigateSubmit";

        #endregion

        /// <summary>
        /// Initializes a new instance of the <see cref="UICanvas"/> class.
        /// </summary>
        public UICanvas()
        {
            _guiRoot = new CanvasRootControl(this)
            {
                IsLayoutLocked = false
            };
        }

        /// <summary>
        /// Finalizes an instance of the <see cref="UICanvas"/> class.
        /// </summary>
        ~UICanvas()
        {
            if (_isRegisteredForTick)
            {
                _isRegisteredForTick = false;
                Scripting.Update -= OnUpdate;
            }
        }

        /// <summary>
        /// Gets the world-space oriented bounding box that contains a 3D canvas.
        /// </summary>
        public OrientedBoundingBox Bounds
        {
            get
            {
                OrientedBoundingBox bounds = new OrientedBoundingBox
                {
                    Extents = new Float3(_guiRoot.Size * 0.5f, Mathf.Epsilon)
                };
                GetWorldMatrix(out Matrix world);
                Matrix.Translation((float)bounds.Extents.X, (float)bounds.Extents.Y, 0, out Matrix offset);
                Matrix.Multiply(ref offset, ref world, out var boxWorld);
                boxWorld.Decompose(out bounds.Transformation);
                return bounds;
            }
        }

        /// <summary>
        /// Gets a value indicating whether canvas is 2D (screen-space).
        /// </summary>
        public bool Is2D => _renderMode == CanvasRenderMode.ScreenSpace;

        /// <summary>
        /// Gets a value indicating whether canvas is 3D (world-space or camera-space).
        /// </summary>
        public bool Is3D => _renderMode != CanvasRenderMode.ScreenSpace;

        /// <summary>
        /// Gets the world matrix used to transform the GUI from the local space to the world space. Handles canvas rendering mode
        /// </summary>
        /// <param name="world">The world.</param>
        public void GetWorldMatrix(out Matrix world)
        {
            GetWorldMatrix(Vector3.Zero, out world);
        }

        /// <summary>
        /// Gets the world matrix used to transform the GUI from the local space to the world space. Handles canvas rendering mode
        /// </summary>
        /// <param name="viewOrigin">The view origin (when using relative-to-camera rendering).</param>
        /// <param name="world">The world.</param>
        public void GetWorldMatrix(Vector3 viewOrigin, out Matrix world)
        {
            var transform = Transform;
            Float3 translation = transform.Translation - viewOrigin;

#if FLAX_EDITOR
            // Override projection for editor preview
            if (_editorTask)
            {
                if (_renderMode == CanvasRenderMode.WorldSpace)
                {
                    Matrix.Transformation(ref transform.Scale, ref transform.Orientation, ref translation, out world);
                }
                else if (_renderMode == CanvasRenderMode.WorldSpaceFaceCamera)
                {
                    var view = _editorTask.View;
                    Matrix.Translation(_guiRoot.Width * -0.5f, _guiRoot.Height * -0.5f, 0, out var m1);
                    Matrix.Scaling(ref transform.Scale, out var m2);
                    Matrix.Multiply(ref m1, ref m2, out var m3);
                    Quaternion.Euler(180, 180, 0, out var quat);
                    Matrix.RotationQuaternion(ref quat, out m2);
                    Matrix.Multiply(ref m3, ref m2, out m1);
                    m2 = Matrix.Transformation(Float3.One, Quaternion.FromDirection(-view.Direction), translation);
                    Matrix.Multiply(ref m1, ref m2, out world);
                }
                else if (_renderMode == CanvasRenderMode.CameraSpace)
                {
                    var view = _editorTask.View;
                    var frustum = view.Frustum;
                    if (!frustum.IsOrthographic)
                        _guiRoot.Size = new Float2(frustum.GetWidthAtDepth(Distance), frustum.GetHeightAtDepth(Distance));
                    else
                        _guiRoot.Size = _editorTask.Viewport.Size;
                    Matrix.Translation(_guiRoot.Width / -2.0f, _guiRoot.Height / -2.0f, 0, out world);
                    Matrix.RotationYawPitchRoll(Mathf.Pi, Mathf.Pi, 0, out var tmp2);
                    Matrix.Multiply(ref world, ref tmp2, out var tmp1);
                    Float3 viewPos = view.Position - viewOrigin;
                    var viewRot = view.Direction != Float3.Up ? Quaternion.LookRotation(view.Direction, Float3.Up) : Quaternion.LookRotation(view.Direction, Float3.Right);
                    var viewUp = Float3.Up * viewRot;
                    var viewForward = view.Direction;
                    var pos = view.Position + view.Direction * Distance;
                    Matrix.Billboard(ref pos, ref viewPos, ref viewUp, ref viewForward, out tmp2);
                    Matrix.Multiply(ref tmp1, ref tmp2, out world);
                    return;
                }
                else
                {
                    world = Matrix.Identity;
                }
                return;
            }
#endif

            // Use default camera is not specified
            var camera = RenderCamera ?? Camera.MainCamera;

            if (_renderMode == CanvasRenderMode.WorldSpace || (_renderMode == CanvasRenderMode.WorldSpaceFaceCamera && !camera))
            {
                // In 3D world
                Matrix.Transformation(ref transform.Scale, ref transform.Orientation, ref translation, out world);
            }
            else if (_renderMode == CanvasRenderMode.WorldSpaceFaceCamera)
            {
                // In 3D world face camera
                Matrix.Translation(_guiRoot.Width * -0.5f, _guiRoot.Height * -0.5f, 0, out var m1);
                Matrix.Scaling(ref transform.Scale, out var m2);
                Matrix.Multiply(ref m1, ref m2, out var m3);
                Quaternion.Euler(180, 180, 0, out var quat);
                Matrix.RotationQuaternion(ref quat, out m2);
                Matrix.Multiply(ref m3, ref m2, out m1);
                m2 = Matrix.Transformation(Vector3.One, Quaternion.FromDirection(-camera.Direction), translation);
                Matrix.Multiply(ref m1, ref m2, out world);
            }
            else if (_renderMode == CanvasRenderMode.CameraSpace && camera)
            {
                Matrix tmp1, tmp2;

                // Adjust GUI size to the viewport size at the given distance form the camera
                var viewport = camera.Viewport;
                if (camera.UsePerspective)
                {
                    camera.GetMatrices(out tmp1, out var tmp3, ref viewport);
                    Matrix.Multiply(ref tmp1, ref tmp3, out tmp2);
                    var frustum = new BoundingFrustum(tmp2);
                    _guiRoot.Size = new Float2(frustum.GetWidthAtDepth(Distance), frustum.GetHeightAtDepth(Distance));
                }
                else
                {
                    _guiRoot.Size = viewport.Size * camera.OrthographicScale;
                }

                // Center viewport (and flip)
                Matrix.Translation(_guiRoot.Width / -2.0f, _guiRoot.Height / -2.0f, 0, out world);
                Matrix.RotationYawPitchRoll(Mathf.Pi, Mathf.Pi, 0, out tmp2);
                Matrix.Multiply(ref world, ref tmp2, out tmp1);

                // In front of the camera
                Float3 viewPos = camera.Position - viewOrigin;
                var viewRot = camera.Orientation;
                var viewUp = Float3.Up * viewRot;
                var viewForward = Float3.Forward * viewRot;
                var pos = viewPos + viewForward * Distance;
                Matrix.Billboard(ref pos, ref viewPos, ref viewUp, ref viewForward, out tmp2);

                Matrix.Multiply(ref tmp1, ref tmp2, out world);
            }
            else
            {
                // Direct projection
                world = Matrix.Identity;
            }
        }

        private void Setup()
        {
            if (_isLoading)
                return;

            switch (_renderMode)
            {
            case CanvasRenderMode.ScreenSpace:
            {
                // Fill the screen area
                _guiRoot.AnchorPreset = AnchorPresets.StretchAll;
                _guiRoot.Offsets = Margin.Zero;
                if (_renderer)
                {
#if FLAX_EDITOR
                    _editorTask?.CustomPostFx.Remove(_renderer);
#endif
                    SceneRenderTask.GlobalCustomPostFx.Remove(_renderer);
                    _renderer.Canvas = null;
                    Destroy(_renderer);
                    _renderer = null;
                }
#if FLAX_EDITOR
                if (_editorRoot != null && IsActiveInHierarchy)
                {
                    _guiRoot.Parent = _editorRoot;
                    _guiRoot.IndexInParent = 0;
                }
#endif
                if (_isRegisteredForTick)
                {
                    _isRegisteredForTick = false;
                    Scripting.Update -= OnUpdate;
                }
                break;
            }
            case CanvasRenderMode.CameraSpace:
            case CanvasRenderMode.WorldSpace:
            case CanvasRenderMode.WorldSpaceFaceCamera:
            {
                // Render canvas manually
                _guiRoot.AnchorPreset = AnchorPresets.TopLeft;
#if FLAX_EDITOR
                if (_editorRoot != null && _guiRoot != null)
                    _guiRoot.Parent = null;
#endif
                if (_renderer == null)
                {
                    _renderer = New<CanvasRenderer>();
                    _renderer.Canvas = this;
                    if (IsActiveInHierarchy && Scene)
                    {
#if FLAX_EDITOR
                        if (_editorTask != null)
                        {
                            _editorTask.CustomPostFx.Add(_renderer);
                            break;
                        }
#endif
                        SceneRenderTask.GlobalCustomPostFx.Add(_renderer);
                    }
#if FLAX_EDITOR
                    else if (_editorTask != null && IsActiveInHierarchy)
                    {
                        _editorTask.CustomPostFx.Add(_renderer);
                    }
#endif
                }
                if (!_isRegisteredForTick)
                {
                    _isRegisteredForTick = true;
                    Scripting.Update += OnUpdate;
                }
                break;
            }
            }
        }

        private void OnUpdate()
        {
            if (this && IsActiveInHierarchy && _renderMode != CanvasRenderMode.ScreenSpace)
            {
                try
                {
                    Profiler.BeginEvent(Name);
                    _guiRoot.Update(Time.UnscaledDeltaTime);
                }
                finally
                {
                    Profiler.EndEvent();
                }
            }
        }

        internal string Serialize()
        {
            StringBuilder sb = new StringBuilder(256);
            StringWriter sw = new StringWriter(sb, CultureInfo.InvariantCulture);
            using (JsonTextWriter jsonWriter = new JsonTextWriter(sw))
            {
                jsonWriter.IndentChar = '\t';
                jsonWriter.Indentation = 1;
                jsonWriter.Formatting = Formatting.Indented;

                jsonWriter.WriteStartObject();

                jsonWriter.WritePropertyName("RenderMode");
                jsonWriter.WriteValue(_renderMode);

                jsonWriter.WritePropertyName("RenderLocation");
                jsonWriter.WriteValue(RenderLocation);

                jsonWriter.WritePropertyName("Order");
                jsonWriter.WriteValue(Order);

                jsonWriter.WritePropertyName("ReceivesEvents");
                jsonWriter.WriteValue(ReceivesEvents);

                jsonWriter.WritePropertyName("IgnoreDepth");
                jsonWriter.WriteValue(IgnoreDepth);

                jsonWriter.WritePropertyName("RenderCamera");
                jsonWriter.WriteValue(Json.JsonSerializer.GetStringID(RenderCamera));

                jsonWriter.WritePropertyName("Distance");
                jsonWriter.WriteValue(Distance);

                if (RenderMode == CanvasRenderMode.WorldSpace || RenderMode == CanvasRenderMode.WorldSpaceFaceCamera)
                {
                    jsonWriter.WritePropertyName("Size");
                    jsonWriter.WriteStartObject();
                    jsonWriter.WritePropertyName("X");
                    jsonWriter.WriteValue(Size.X);
                    jsonWriter.WritePropertyName("Y");
                    jsonWriter.WriteValue(Size.Y);
                    jsonWriter.WriteEndObject();
                }

                jsonWriter.WritePropertyName("NavigationInputRepeatDelay");
                jsonWriter.WriteValue(NavigationInputRepeatDelay);
                jsonWriter.WritePropertyName("NavigationInputRepeatRate");
                jsonWriter.WriteValue(NavigationInputRepeatRate);
                jsonWriter.WritePropertyName("NavigationInputActionUp");
                jsonWriter.WriteValue(NavigationInputActionUp);
                jsonWriter.WritePropertyName("NavigationInputActionDown");
                jsonWriter.WriteValue(NavigationInputActionDown);
                jsonWriter.WritePropertyName("NavigationInputActionLeft");
                jsonWriter.WriteValue(NavigationInputActionLeft);
                jsonWriter.WritePropertyName("NavigationInputActionRight");
                jsonWriter.WriteValue(NavigationInputActionRight);

                jsonWriter.WriteEndObject();
            }

            return sw.ToString();
        }

        internal string SerializeDiff(UICanvas other)
        {
            StringBuilder sb = new StringBuilder(256);
            StringWriter sw = new StringWriter(sb, CultureInfo.InvariantCulture);
            using (JsonTextWriter jsonWriter = new JsonTextWriter(sw))
            {
                jsonWriter.IndentChar = '\t';
                jsonWriter.Indentation = 1;
                jsonWriter.Formatting = Formatting.Indented;

                jsonWriter.WriteStartObject();

                if (_renderMode != other._renderMode)
                {
                    jsonWriter.WritePropertyName("RenderMode");
                    jsonWriter.WriteValue(_renderMode);
                }

                if (RenderLocation != other.RenderLocation)
                {
                    jsonWriter.WritePropertyName("RenderLocation");
                    jsonWriter.WriteValue(RenderLocation);
                }

                if (Order != other.Order)
                {
                    jsonWriter.WritePropertyName("Order");
                    jsonWriter.WriteValue(Order);
                }

                if (ReceivesEvents != other.ReceivesEvents)
                {
                    jsonWriter.WritePropertyName("ReceivesEvents");
                    jsonWriter.WriteValue(ReceivesEvents);
                }

                if (IgnoreDepth != other.IgnoreDepth)
                {
                    jsonWriter.WritePropertyName("IgnoreDepth");
                    jsonWriter.WriteValue(IgnoreDepth);
                }

                if (RenderCamera != other.RenderCamera)
                {
                    jsonWriter.WritePropertyName("RenderCamera");
                    jsonWriter.WriteValue(Json.JsonSerializer.GetStringID(RenderCamera));
                }

                if (Mathf.Abs(Distance - other.Distance) > Mathf.Epsilon)
                {
                    jsonWriter.WritePropertyName("Distance");
                    jsonWriter.WriteValue(Distance);
                }

                if ((RenderMode == CanvasRenderMode.WorldSpace ||
                     RenderMode == CanvasRenderMode.WorldSpaceFaceCamera ||
                     other.RenderMode == CanvasRenderMode.WorldSpace ||
                     other.RenderMode == CanvasRenderMode.WorldSpaceFaceCamera) && Size != other.Size)
                {
                    jsonWriter.WritePropertyName("Size");
                    jsonWriter.WriteStartObject();
                    jsonWriter.WritePropertyName("X");
                    jsonWriter.WriteValue(Size.X);
                    jsonWriter.WritePropertyName("Y");
                    jsonWriter.WriteValue(Size.Y);
                    jsonWriter.WriteEndObject();
                }

                if (!Mathf.NearEqual(NavigationInputRepeatDelay, other.NavigationInputRepeatDelay))
                {
                    jsonWriter.WritePropertyName("NavigationInputRepeatDelay");
                    jsonWriter.WriteValue(NavigationInputRepeatDelay);
                }
                if (!Mathf.NearEqual(NavigationInputRepeatRate, other.NavigationInputRepeatRate))
                {
                    jsonWriter.WritePropertyName("NavigationInputRepeatRate");
                    jsonWriter.WriteValue(NavigationInputRepeatRate);
                }
                if (NavigationInputActionUp != other.NavigationInputActionUp)
                {
                    jsonWriter.WritePropertyName("NavigationInputActionUp");
                    jsonWriter.WriteValue(NavigationInputActionUp);
                }
                if (NavigationInputActionDown != other.NavigationInputActionDown)
                {
                    jsonWriter.WritePropertyName("NavigationInputActionDown");
                    jsonWriter.WriteValue(NavigationInputActionDown);
                }
                if (NavigationInputActionLeft != other.NavigationInputActionLeft)
                {
                    jsonWriter.WritePropertyName("NavigationInputActionLeft");
                    jsonWriter.WriteValue(NavigationInputActionLeft);
                }
                if (NavigationInputActionRight != other.NavigationInputActionRight)
                {
                    jsonWriter.WritePropertyName("NavigationInputActionRight");
                    jsonWriter.WriteValue(NavigationInputActionRight);
                }

                jsonWriter.WriteEndObject();
            }

            return sw.ToString();
        }

        internal void Deserialize(string json)
        {
            _isLoading = true;
            Json.JsonSerializer.Deserialize(this, json);
            _isLoading = false;
        }

        internal void PostDeserialize()
        {
            Setup();
        }

        internal void ParentChanged()
        {
#if FLAX_EDITOR
            if (RenderMode == CanvasRenderMode.ScreenSpace && _editorRoot != null && _guiRoot != null)
            {
                _guiRoot.Parent = IsActiveInHierarchy ? _editorRoot : null;
                _guiRoot.IndexInParent = 0;
            }
#endif
        }

        internal void OnEnable()
        {
#if FLAX_EDITOR
            if (_editorRoot != null)
            {
                _guiRoot.Parent = _editorRoot;
                _guiRoot.IndexInParent = 0;
            }
            else
            {
                _guiRoot.Parent = RootControl.CanvasRoot;
            }
#else
            _guiRoot.Parent = RootControl.CanvasRoot;
#endif

            if (_renderer)
            {
#if FLAX_EDITOR
                if (_editorTask != null)
                {
                    _editorTask.CustomPostFx.Add(_renderer);
                    return;
                }
#endif
                SceneRenderTask.GlobalCustomPostFx.Add(_renderer);
            }
        }

        internal void OnDisable()
        {
            _guiRoot.Parent = null;

            if (_renderer)
            {
                SceneRenderTask.GlobalCustomPostFx.Remove(_renderer);
            }
        }

#if FLAX_EDITOR
        internal void OnActiveInTreeChanged()
        {
            if (RenderMode == CanvasRenderMode.ScreenSpace && _editorRoot != null && _guiRoot != null)
            {
                _guiRoot.Parent = IsActiveInHierarchy ? _editorRoot : null;
                _guiRoot.IndexInParent = 0;
            }
        }
#endif

        internal void EndPlay()
        {
            if (_isRegisteredForTick)
            {
                _isRegisteredForTick = false;
                Scripting.Update -= OnUpdate;
            }

            if (_renderer)
            {
                SceneRenderTask.GlobalCustomPostFx.Remove(_renderer);
                _renderer.Canvas = null;
                Destroy(_renderer);
                _renderer = null;
            }
        }

#if FLAX_EDITOR
        private SceneRenderTask _editorTask;
        private ContainerControl _editorRoot;

        internal void EditorOverride(SceneRenderTask task, ContainerControl root)
        {
            if (_editorTask == task && _editorRoot == root)
                return;
            if (_editorTask != null && _renderer != null)
                _editorTask.CustomPostFx.Remove(_renderer);
            if (_editorRoot != null && _guiRoot != null)
                _guiRoot.Parent = null;

            _editorTask = task;
            _editorRoot = root;
            Setup();

            if (RenderMode == CanvasRenderMode.ScreenSpace && _editorRoot != null && _guiRoot != null && IsActiveInHierarchy)
            {
                _guiRoot.Parent = _editorRoot;
                _guiRoot.IndexInParent = 0;
            }
        }
#endif
    }
}
