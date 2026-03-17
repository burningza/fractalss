import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

// ============================================
// Configuration State
// ============================================
const state = {
    fractalType: 'mandelbulb',
    iterations: 8,
    zoom: 2.0,
    colorScheme: 'classic',
    juliaX: 0.3,
    juliaY: 0.5,
    power: 8,
    rotationSpeed: 0.3,
    time: 0,
    // Attractor parameters
    attractorA: 1.0,
    attractorB: 1.8,
    attractorC: 0.7,
    attractorD: 1.2,
    attractorScale: 1.0,
    lorenzSigma: 10.0,
    lorenzRho: 28.0,
    lorenzBeta: 2.67,
    is3D: true
};

// Map fractal types to shader integers
const fractalTypeMap = {
    'mandelbulb': 0,
    'julia': 1,
    'menger': 2,
    'sierpinski': 3,
    'pickover': 4,
    'dejong': 5,
    'tinkerbell': 6,
    'lorenz': 7
};

// Map color schemes to shader integers
const colorSchemeMap = {
    'classic': 0,
    'fire': 1,
    'ocean': 2,
    'neon': 3
};

// ============================================
// Three.js Setup
// ============================================
const canvas = document.getElementById('fractal-canvas');
const renderer = new THREE.WebGLRenderer({
    canvas,
    antialias: false
});
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(75, window.innerWidth / window.innerHeight, 0.1, 100);
camera.position.set(0, 0, -3);

// ============================================
// Fractal Ray-Marching Shader
// ============================================
const fractalVertexShader = `
    varying vec2 vUv;

    void main() {
        vUv = uv;
        gl_Position = vec4(position, 1.0);
    }
`;

const fractalFragmentShader = `
    precision highp float;

    uniform vec2 resolution;
    uniform vec3 cameraPos;
    uniform mat4 cameraRotation;
    uniform float time;
    uniform int maxIterations;
    uniform float zoom;
    uniform int fractalType;
    uniform vec2 juliaC;
    uniform float power;
    uniform int colorScheme;
    uniform float fov;

    varying vec2 vUv;

    #define MAX_ITER 100
    #define MAX_DIST 20.0
    #define SURF_DIST 0.001

    // Smooth iteration count for anti-aliasing
    float smoothIter(float l, float r, float escapeRadius) {
        return l - log(log(escapeRadius) / log(2.0)) / log(2.0);
    }

    // Color palettes
    vec3 palette(float t, vec3 a, vec3 b, vec3 c, vec3 d) {
        return a + b * cos(6.28318 * (c * t + d));
    }

    vec3 getColor(float iter, float smoothIterVal, int scheme) {
        float t = clamp(smoothIterVal / float(maxIterations), 0.0, 1.0);

        if (scheme == 0) { // Classic
            return palette(t, vec3(0.5), vec3(0.5), vec3(1.0), vec3(0.0, 0.33, 0.67));
        } else if (scheme == 1) { // Fire
            return vec3(
                smoothstep(0.0, 0.33, t),
                smoothstep(0.33, 0.66, t),
                smoothstep(0.66, 1.0, t)
            );
        } else if (scheme == 2) { // Ocean
            return vec3(0.0, 0.3 + t * 0.5, 0.6 + t * 0.4);
        } else { // Neon
            return palette(t, vec3(0.5), vec3(0.5), vec3(1.0), vec3(0.0, 0.1, 0.2));
        }
    }

    // Mandelbulb distance estimator
    float mandelbulbDE(vec3 pos) {
        vec3 z = pos;
        float dr = 1.0;
        float r = 0.0;

        for (int i = 0; i < MAX_ITER; i++) {
            if (i >= maxIterations) break;

            r = length(z);
            if (r > 2.0) break;

            float theta = acos(clamp(z.z / r, -1.0, 1.0));
            float phi = atan(z.y, z.x);
            dr = pow(r, power - 1.0) * dr * power + 1.0;

            float zr = pow(r, power);
            theta *= power;
            phi *= power;

            z = zr * vec3(
                sin(theta) * cos(phi),
                sin(phi) * sin(theta),
                cos(theta)
            );
            z += pos;
        }

        return 0.5 * log(r) * r / dr;
    }

    // 3D Julia Set using quaternion math
    float juliaDE(vec3 pos) {
        vec4 z = vec4(pos, 0.0);
        vec4 c = vec4(juliaC.x, 0.0, 0.0, juliaC.y);
        float dr = 1.0;

        for (int i = 0; i < MAX_ITER; i++) {
            if (i >= maxIterations) break;

            float r = length(z);
            if (r > 2.0) break;

            // Quaternion multiplication: z = z^2 + c
            dr = 2.0 * r * dr;

            float r2 = dot(z, z);
            vec4 newZ = vec4(
                z.x * z.x - z.y * z.y - z.z * z.z - z.w * z.w,
                2.0 * z.x * z.y,
                2.0 * z.x * z.z,
                2.0 * z.x * z.w
            );
            z = newZ + c;
        }

        return length(z) / dr;
    }

    // Menger Sponge
    float mengerDE(vec3 pos) {
        float scale = 3.0;
        vec3 offset = vec3(1.0);
        float s = 1.0;

        for (int i = 0; i < MAX_ITER; i++) {
            if (i >= maxIterations) break;

            pos = abs(pos);
            if (pos.x < pos.y) pos.xy = pos.yx;
            if (pos.x < pos.z) pos.xz = pos.zx;
            if (pos.y < pos.z) pos.yz = pos.zy;

            pos = scale * pos - offset * (scale - 1.0);
            s *= scale;
        }

        vec3 d = abs(pos) - vec3(1.0);
        float d1 = min(min(d.x, d.y), d.z);
        float d2 = max(max(d.x, d.y), d.z);
        return max(d1, -d2) / s;
    }

    // Sierpinski Tetrahedron
    float sierpinskiDE(vec3 pos) {
        float scale = 2.0;
        vec3 offset = vec3(1.0);

        for (int i = 0; i < MAX_ITER; i++) {
            if (i >= maxIterations) break;

            pos = abs(pos);
            if (pos.x + pos.y < 0.0) pos.xy = -pos.yx;
            if (pos.x + pos.z < 0.0) pos.xz = -pos.zx;
            if (pos.y + pos.z < 0.0) pos.yz = -pos.zy;

            pos = scale * pos - offset * (scale - 1.0);
        }

        return length(pos) * pow(scale, -float(maxIterations));
    }

    // Pickover Attractor (SDF Approximation - just a sphere for raymarching)
    float pickoverDE(vec3 pos) {
        return length(pos) - 1.0; 
    }

    float getDistance(vec3 pos) {
        pos *= zoom;
        float d = mandelbulbDE(pos);
        if (fractalType == 0) d = mandelbulbDE(pos);
        else if (fractalType == 1) d = juliaDE(pos);
        else if (fractalType == 2) d = mengerDE(pos);
        else if (fractalType == 3) d = sierpinskiDE(pos);
        else if (fractalType == 4) d = pickoverDE(pos);
        
        return d / zoom;
    }

    // Normal calculation using gradient
    vec3 calcNormal(vec3 pos) {
        const float h = 0.0001;
        const vec2 k = vec2(1, -1);
        return normalize(
            k.xyy * getDistance(pos + k.xyy * h) +
            k.yyx * getDistance(pos + k.yyx * h) +
            k.yxy * getDistance(pos + k.yxy * h) +
            k.xxx * getDistance(pos + k.xxx * h)
        );
    }

    // Soft shadows
    float softShadow(vec3 ro, vec3 rd, float mint, float maxt, float k) {
        float res = 1.0;
        float t = mint;
        for (int i = 0; i < 32; i++) {
            float h = getDistance(ro + rd * t);
            res = min(res, k * h / t);
            t += clamp(h, 0.01, 0.1);
            if (res < 0.001 || t > maxt) break;
        }
        return clamp(res, 0.0, 1.0);
    }

    // Ambient occlusion
    float calcAO(vec3 pos, vec3 nor) {
        float occ = 0.0;
        float sca = 1.0;
        for (int i = 0; i < 5; i++) {
            float h = 0.01 + 0.12 * float(i) / 4.0;
            float d = getDistance(pos + h * nor);
            occ += (h - d) * sca;
            sca *= 0.95;
        }
        return clamp(1.0 - 3.0 * occ, 0.0, 1.0);
    }

    void main() {
        // Ray direction from camera through pixel
        vec2 uv = (vUv - 0.5) * 2.0;
        uv.x *= resolution.x / resolution.y;

        float camScale = tan(fov * 0.5);
        vec3 rayDir = normalize(vec3(uv * camScale, -1.0));

        // Apply camera rotation
        rayDir = (cameraRotation * vec4(rayDir, 0.0)).xyz;
        vec3 rayOrigin = cameraPos;

        vec3 col = vec3(0.0);
        float t = 0.0;
        int iter = 0;
        float smoothIter = 0.0;

        // Ray marching
        for (int i = 0; i < 128; i++) {

            vec3 pos = rayOrigin + rayDir * t;
            float d = getDistance(pos);

            if (d < SURF_DIST) {
                iter = i;
                smoothIter = float(i) - log(log(length(pos))) / log(2.0);
                break;
            }

            if (t > MAX_DIST) {
                iter = MAX_ITER;
                break;
            }

            t += d * 0.8;
        }

        // Background gradient
        if (iter >= MAX_ITER) {
            float bg = 0.02 + 0.01 * (vUv.y);
            col = vec3(bg);
        } else {
            vec3 pos = rayOrigin + rayDir * t;
            vec3 nor = calcNormal(pos);

            // Lighting
            vec3 lightPos = vec3(2.0, 3.0, 4.0);
            vec3 lightDir = normalize(lightPos - pos);
            vec3 viewDir = normalize(rayOrigin - pos);

            // Diffuse
            float diff = max(dot(nor, lightDir), 0.0);

            // Specular
            vec3 halfDir = normalize(lightDir + viewDir);
            float spec = pow(max(dot(nor, halfDir), 0.0), 32.0);

            // Ambient occlusion
            float ao = calcAO(pos, nor);

            // Shadow
            float shadow = softShadow(pos + nor * 0.02, lightDir, 0.02, 2.5, 8.0);

            // Color
            vec3 baseColor = getColor(float(iter), smoothIter, colorScheme);

            // Combine lighting
            vec3 ambient = baseColor * 0.15 * ao;
            vec3 diffuse = baseColor * diff * 0.7 * shadow;
            vec3 specular = vec3(1.0) * spec * 0.3 * shadow;
            col = ambient + diffuse + specular;

            // Fog
            float fog = exp(-t * 0.05);
            col = mix(vec3(0.0), col, fog);
        }

        // Tone mapping
        col = col / (col + vec3(1.0));
        col = pow(col, vec3(1.0 / 2.2));

        gl_FragColor = vec4(col, 1.0);
    }
`;

// Full-screen quad geometry
const quadGeometry = new THREE.PlaneGeometry(2, 2);
const material = new THREE.ShaderMaterial({
    vertexShader: fractalVertexShader,
    fragmentShader: fractalFragmentShader,
    uniforms: {
        resolution: { value: new THREE.Vector2(window.innerWidth, window.innerHeight) },
        cameraPos: { value: new THREE.Vector3(0, 0, -3) },
        cameraRotation: { value: new THREE.Matrix4() },
        time: { value: 0 },
        maxIterations: { value: state.iterations },
        zoom: { value: state.zoom },
        fractalType: { value: fractalTypeMap[state.fractalType] },
        juliaC: { value: new THREE.Vector2(state.juliaX, state.juliaY) },
        power: { value: state.power },
        colorScheme: { value: colorSchemeMap[state.colorScheme] },
        fov: { value: Math.PI / 4 }
    },
    depthWrite: false,
    depthTest: false
});

const quad = new THREE.Mesh(quadGeometry, material);
quad.frustumCulled = false;

// Pickover Attractor - Point Cloud implementation
const pickoverCount = 100000;
const pickoverGeometry = new THREE.BufferGeometry();
const pickoverPositions = new Float32Array(pickoverCount * 3);
const pickoverColors = new Float32Array(pickoverCount * 3);

pickoverGeometry.setAttribute('position', new THREE.BufferAttribute(pickoverPositions, 3));
pickoverGeometry.setAttribute('color', new THREE.BufferAttribute(pickoverColors, 3));

// Particle shapes using canvas
function createParticleTexture(shape) {
    const canvas = document.createElement('canvas');
    canvas.width = 64;
    canvas.height = 64;
    const ctx = canvas.getContext('2d');
    
    ctx.fillStyle = 'white';
    ctx.clearRect(0, 0, 64, 64);
    
    if (shape === 'circle') {
        ctx.beginPath();
        ctx.arc(32, 32, 32, 0, Math.PI * 2);
        ctx.fill();
    } else if (shape === 'diamond') {
        ctx.beginPath();
        ctx.moveTo(32, 0);
        ctx.lineTo(64, 32);
        ctx.lineTo(32, 64);
        ctx.lineTo(0, 32);
        ctx.fill();
    } else { // square
        ctx.fillRect(0, 0, 64, 64);
    }
    
    return new THREE.CanvasTexture(canvas);
}

const particleTextures = {
    square: createParticleTexture('square'),
    circle: createParticleTexture('circle'),
    diamond: createParticleTexture('diamond')
};

const pickoverMaterial = new THREE.PointsMaterial({
    size: 0.015,
    vertexColors: true,
    transparent: true,
    opacity: 0.8,
    blending: THREE.AdditiveBlending,
    map: particleTextures.square,
    alphaTest: 0.1
});

const pickoverPoints = new THREE.Points(pickoverGeometry, pickoverMaterial);
pickoverPoints.visible = false;

// Create a common group for rotation
const fractalMesh = new THREE.Group();
fractalMesh.add(quad);
fractalMesh.add(pickoverPoints);
scene.add(fractalMesh);

function updateAttractorPoints() {
    let x, y, z;
    const s = state.attractorScale;

    const posAttr = pickoverGeometry.attributes.position.array;
    const colAttr = pickoverGeometry.attributes.color.array;

    if (state.fractalType === 'lorenz') {
        x = 0.1; y = 0; z = 0;
        const dt = 0.01;
        const sigma = state.lorenzSigma;
        const rho = state.lorenzRho;
        const beta = state.lorenzBeta;

        for (let i = 0; i < pickoverCount; i++) {
            const dx = sigma * (y - x) * dt;
            const dy = (x * (rho - z) - y) * dt;
            const dz = (x * y - beta * z) * dt;

            x += dx; y += dy; z += dz;

            posAttr[i * 3] = x * s;
            posAttr[i * 3 + 1] = y * s;
            posAttr[i * 3 + 2] = (z - 25) * s; 

            colAttr[i * 3] = Math.abs(x) / 20.0 + 0.5;
            colAttr[i * 3 + 1] = Math.abs(y) / 20.0 + 0.5;
            colAttr[i * 3 + 2] = Math.abs(z - 25) / 20.0 + 0.5;
        }
    } else {
        x = 0.1; y = 0.1; z = 0.1;
        const a = state.attractorA;
        const b = state.attractorB;
        const c = state.attractorC;
        const d = state.attractorD;

        for (let i = 0; i < pickoverCount; i++) {
            let hx, hy, hz = 0;

            if (state.fractalType === 'pickover') {
                hx = Math.sin(a * y) - (state.is3D ? z * Math.cos(b * x) : Math.cos(b * x));
                hy = (state.is3D ? z * Math.sin(c * x) : Math.sin(c * x)) - Math.cos(d * y);
                hz = state.is3D ? Math.sin(x) : 0;
            } else if (state.fractalType === 'dejong') {
                hx = Math.sin(a * y) - Math.cos(b * x);
                hy = Math.sin(c * x) - Math.cos(d * y);
                hz = state.is3D ? Math.sin(a * x) : 0; 
            } else if (state.fractalType === 'tinkerbell') {
                hx = x*x - y*y + a*x + b*y;
                hy = 2*x*y + c*x + d*y;
                hz = state.is3D ? Math.sin(x*y) : 0; 
            }

            x = hx; y = hy; z = hz;

            if (x > 100 || x < -100 || isNaN(x)) x = 0.1;
            if (y > 100 || y < -100 || isNaN(y)) y = 0.1;

            posAttr[i * 3] = x * s;
            posAttr[i * 3 + 1] = y * s;
            posAttr[i * 3 + 2] = z * s;

            colAttr[i * 3] = Math.abs(x) / 2.0 + 0.5;
            colAttr[i * 3 + 1] = Math.abs(y) / 2.0 + 0.5;
            colAttr[i * 3 + 2] = Math.abs(z) / 2.0 + 0.5;
        }
    }

    pickoverGeometry.attributes.position.needsUpdate = true;
    pickoverGeometry.attributes.color.needsUpdate = true;
}

function updateMathInfo() {
    const mathInfo = document.getElementById('math-info');
    const fractalName = document.getElementById('fractal-name');

    let info = '';
    switch (state.fractalType) {
        case 'mandelbulb':
            fractalName.textContent = 'Mandelbulb';
            info = '<strong>Discovered by:</strong> Daniel White and Paul Nylander (2009)<br><br>A 3D analog of the Mandelbrot set. It is constructed by using spherical coordinates to raise a 3D vector to a power <em>n</em>. Defined by the iterative formula: <em>z = z^n + c</em>, where <em>n</em> is the power.';
            break;
        case 'julia':
            fractalName.textContent = 'Julia Set (3D)';
            info = '<strong>First published by:</strong> Gaston Julia (1918)<br><br>While the original 2D sets were studied by Gaston Julia in 1918, this 3D version is a slice of a 4D quaternion Julia set. It explores the behavior of the complex quadratic polynomial: <em>z = z^2 + c</em> in 3D space, where <em>c</em> = (' + state.juliaX.toFixed(2) + ', ' + state.juliaY.toFixed(2) + ').';
            break;
        case 'menger':
            fractalName.textContent = 'Menger Sponge';
            info = '<strong>First published by:</strong> Karl Menger (1926)<br><br>A 3D fractal curve and a generalization of the Cantor set and Sierpinski carpet. It is a universal curve, meaning it has a topological dimension of one, yet its surface area is infinite and its volume is zero.';
            break;
        case 'sierpinski':
            fractalName.textContent = 'Sierpinski Tetrahedron';
            info = '<strong>First published by:</strong> Wacław Sierpiński (1915)<br><br>Also known as the Tetrix. It is a 3D extension of the Sierpinski triangle, formed by repeatedly shrinking a regular tetrahedron to half its height, making four copies, and placing them at the corners.';
            break;
        case 'pickover':
            fractalName.textContent = 'Pickover Attractor';
            info = '<strong>First published by:</strong> Clifford A. Pickover (1990)<br><br>A chaotic strange attractor defined by complex trigonometric recurrences. This visualization calculates the point positions iteratively using the formulas: <br><em>x = sin(a*y) - z*cos(b*x)</em><br><em>y = z*sin(c*x) - cos(d*y)</em><br><em>z = sin(x)</em>';
            break;
        case 'dejong':
            fractalName.textContent = 'Peter de Jong Attractor';
            info = '<strong>Discovered by:</strong> Peter de Jong (1991)<br><br>A beautifully swirling chaotic map generated by evaluating sinusoidal equations. It produces organic, wispy shapes that resemble smoke or fluid dynamics.<br><br><em>x = sin(a*y) - cos(b*x)</em><br><em>y = sin(c*x) - cos(d*y)</em>';
            break;
        case 'tinkerbell':
            fractalName.textContent = 'Tinkerbell Attractor';
            info = '<strong>Origin:</strong> Term coined by Nusse and Yorke (1996)<br><br>A discrete-time dynamical system that resembles the path of a flying fairy. It features distinct swooping trajectories that fold in on themselves.<br><br><em>x = x² - y² + a*x + b*y</em><br><em>y = 2*x*y + c*x + d*y</em>';
            break;
        case 'lorenz':
            fractalName.textContent = 'Lorenz Attractor';
            info = '<strong>First published by:</strong> Edward Lorenz (1963)<br><br>Originally developed as a simplified mathematical model for atmospheric convection. It is a system of three ordinary differential equations and is notable for having chaotic solutions for certain parameter values, commonly looking like a butterfly.<br><br><em>dx/dt = σ(y - x)</em><br><em>dy/dt = x(ρ - z) - y</em><br><em>dz/dt = xy - βz</em>';
            break;
    }
    mathInfo.innerHTML = '<p>' + info + '</p>';
}

// ============================================
// Camera & Controls Setup
// ============================================
const cameraPos = new THREE.Vector3(0, 0, -3);
const cameraTarget = new THREE.Vector3(0, 0, 0);
const cameraUp = new THREE.Vector3(0, 1, 0);

let cameraMatrix = new THREE.Matrix4();
cameraMatrix.lookAt(camera.position, cameraTarget, cameraUp);
material.uniforms.cameraRotation.value = cameraMatrix;

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.05;
controls.target.set(0, 0, 0);
controls.update();

function updateUIVisibility() {
    const juliaControls = document.querySelector('.julia-controls');
    juliaControls.style.display = state.fractalType === 'julia' ? 'block' : 'none';

    const usesABCD = ['pickover', 'dejong', 'tinkerbell'].includes(state.fractalType);
    const abcdControls = document.querySelector('.attractor-abcd-controls');
    if (abcdControls) abcdControls.style.display = usesABCD ? 'block' : 'none';

    const lorenzControls = document.querySelector('.lorenz-controls');
    if (lorenzControls) lorenzControls.style.display = state.fractalType === 'lorenz' ? 'block' : 'none';

    const isAttractor = ['pickover', 'dejong', 'tinkerbell', 'lorenz'].includes(state.fractalType);
    const sharedControls = document.querySelector('.attractor-shared-controls');
    if (sharedControls) sharedControls.style.display = isAttractor ? 'block' : 'none';

    const powerControls = document.querySelector('.power-controls');
    if (powerControls) powerControls.style.display = state.fractalType === 'mandelbulb' ? 'block' : 'none';
}

// Event listeners for controls
document.getElementById('fractal-type').addEventListener('change', (e) => {
    state.fractalType = e.target.value;
    material.uniforms.fractalType.value = fractalTypeMap[state.fractalType];

    updateUIVisibility();

    // Set optimal defaults for the selected fractal
    if (state.fractalType === 'mandelbulb') {
        state.zoom = 2.0; state.power = 8; state.iterations = 8;
    } else if (state.fractalType === 'julia') {
        state.zoom = 2.0; state.juliaX = 0.3; state.juliaY = 0.5; state.iterations = 8;
    } else if (state.fractalType === 'menger') {
        state.zoom = 1.0; state.iterations = 5;
    } else if (state.fractalType === 'sierpinski') {
        state.zoom = 1.5; state.iterations = 8;
    } else if (state.fractalType === 'pickover') {
        state.zoom = 2.0; state.attractorA = 1.0; state.attractorB = 1.8; state.attractorC = 0.7; state.attractorD = 1.2; state.attractorScale = 1.0;
    } else if (state.fractalType === 'dejong') {
        state.zoom = 2.0; state.attractorA = -2.24; state.attractorB = 0.43; state.attractorC = -0.65; state.attractorD = -2.43; state.attractorScale = 1.5;
    } else if (state.fractalType === 'tinkerbell') {
        state.zoom = 2.0; state.attractorA = 0.9; state.attractorB = -0.6013; state.attractorC = 2.0; state.attractorD = 0.50; state.attractorScale = 2.5;
    } else if (state.fractalType === 'lorenz') {
        state.zoom = 2.0; state.lorenzSigma = 10.0; state.lorenzRho = 28.0; state.lorenzBeta = 2.67; state.attractorScale = 0.05;
    }

    // Update uniform values and UI
    material.uniforms.zoom.value = state.zoom;
    document.getElementById('zoom').value = state.zoom;
    document.getElementById('zoom-val').textContent = state.zoom.toFixed(1);

    material.uniforms.maxIterations.value = state.iterations;
    document.getElementById('iterations').value = state.iterations;
    document.getElementById('iter-val').textContent = state.iterations;

    if (state.fractalType === 'mandelbulb') {
        material.uniforms.power.value = state.power;
        document.getElementById('power').value = state.power;
        document.getElementById('power-val').textContent = state.power;
    } else if (state.fractalType === 'julia') {
        material.uniforms.juliaC.value.set(state.juliaX, state.juliaY);
        document.getElementById('julia-x').value = state.juliaX;
        document.getElementById('julia-x-val').textContent = state.juliaX.toFixed(2);
        document.getElementById('julia-y').value = state.juliaY;
        document.getElementById('julia-y-val').textContent = state.juliaY.toFixed(2);
    } else if (['pickover', 'dejong', 'tinkerbell'].includes(state.fractalType)) {
        document.getElementById('attractor-a').value = state.attractorA;
        document.getElementById('attractor-a-val').textContent = state.attractorA.toFixed(2);
        document.getElementById('attractor-b').value = state.attractorB;
        document.getElementById('attractor-b-val').textContent = state.attractorB.toFixed(2);
        document.getElementById('attractor-c').value = state.attractorC;
        document.getElementById('attractor-c-val').textContent = state.attractorC.toFixed(2);
        document.getElementById('attractor-d').value = state.attractorD;
        document.getElementById('attractor-d-val').textContent = state.attractorD.toFixed(2);
    } else if (state.fractalType === 'lorenz') {
        document.getElementById('lorenz-sigma').value = state.lorenzSigma;
        document.getElementById('lorenz-sigma-val').textContent = state.lorenzSigma.toFixed(1);
        document.getElementById('lorenz-rho').value = state.lorenzRho;
        document.getElementById('lorenz-rho-val').textContent = state.lorenzRho.toFixed(1);
        document.getElementById('lorenz-beta').value = state.lorenzBeta;
        document.getElementById('lorenz-beta-val').textContent = state.lorenzBeta.toFixed(2);
    }
    
    if (['pickover', 'dejong', 'tinkerbell', 'lorenz'].includes(state.fractalType)) {
        document.getElementById('attractor-scale').value = state.attractorScale;
        document.getElementById('attractor-scale-val').textContent = state.attractorScale.toFixed(2);
    }

    // Show/Hide points vs raymarching quad
    const isAttractor = ['pickover', 'dejong', 'tinkerbell', 'lorenz'].includes(state.fractalType);
    if (isAttractor) {
        pickoverPoints.visible = true;
        quad.visible = false;
        updateAttractorPoints();
    } else {
        pickoverPoints.visible = false;
        quad.visible = true;
    }

    updateMathInfo();
});

document.getElementById('iterations').addEventListener('input', (e) => {
    state.iterations = parseInt(e.target.value);
    document.getElementById('iter-val').textContent = state.iterations;
    material.uniforms.maxIterations.value = state.iterations;
});

document.getElementById('zoom').addEventListener('input', (e) => {
    state.zoom = parseFloat(e.target.value);
    document.getElementById('zoom-val').textContent = state.zoom.toFixed(1);
    material.uniforms.zoom.value = state.zoom;
});

document.getElementById('color-scheme').addEventListener('change', (e) => {
    state.colorScheme = e.target.value;
    material.uniforms.colorScheme.value = {
        'classic': 0,
        'fire': 1,
        'ocean': 2,
        'neon': 3
    }[state.colorScheme];
});

document.getElementById('julia-x').addEventListener('input', (e) => {
    state.juliaX = parseFloat(e.target.value);
    document.getElementById('julia-x-val').textContent = state.juliaX.toFixed(2);
    material.uniforms.juliaC.value.x = state.juliaX;
});

document.getElementById('julia-y').addEventListener('input', (e) => {
    state.juliaY = parseFloat(e.target.value);
    document.getElementById('julia-y-val').textContent = state.juliaY.toFixed(2);
    material.uniforms.juliaC.value.y = state.juliaY;
});

// Attractor ABCD listeners
document.getElementById('attractor-a').addEventListener('input', (e) => {
    state.attractorA = parseFloat(e.target.value);
    document.getElementById('attractor-a-val').textContent = state.attractorA.toFixed(2);
    if (['pickover', 'dejong', 'tinkerbell'].includes(state.fractalType)) updateAttractorPoints();
});

document.getElementById('attractor-b').addEventListener('input', (e) => {
    state.attractorB = parseFloat(e.target.value);
    document.getElementById('attractor-b-val').textContent = state.attractorB.toFixed(2);
    if (['pickover', 'dejong', 'tinkerbell'].includes(state.fractalType)) updateAttractorPoints();
});

document.getElementById('attractor-c').addEventListener('input', (e) => {
    state.attractorC = parseFloat(e.target.value);
    document.getElementById('attractor-c-val').textContent = state.attractorC.toFixed(2);
    if (['pickover', 'dejong', 'tinkerbell'].includes(state.fractalType)) updateAttractorPoints();
});

document.getElementById('attractor-d').addEventListener('input', (e) => {
    state.attractorD = parseFloat(e.target.value);
    document.getElementById('attractor-d-val').textContent = state.attractorD.toFixed(2);
    if (['pickover', 'dejong', 'tinkerbell'].includes(state.fractalType)) updateAttractorPoints();
});

// Lorenz Listeners
document.getElementById('lorenz-sigma').addEventListener('input', (e) => {
    state.lorenzSigma = parseFloat(e.target.value);
    document.getElementById('lorenz-sigma-val').textContent = state.lorenzSigma.toFixed(1);
    if (state.fractalType === 'lorenz') updateAttractorPoints();
});

document.getElementById('lorenz-rho').addEventListener('input', (e) => {
    state.lorenzRho = parseFloat(e.target.value);
    document.getElementById('lorenz-rho-val').textContent = state.lorenzRho.toFixed(1);
    if (state.fractalType === 'lorenz') updateAttractorPoints();
});

document.getElementById('lorenz-beta').addEventListener('input', (e) => {
    state.lorenzBeta = parseFloat(e.target.value);
    document.getElementById('lorenz-beta-val').textContent = state.lorenzBeta.toFixed(2);
    if (state.fractalType === 'lorenz') updateAttractorPoints();
});

document.getElementById('attractor-scale').addEventListener('input', (e) => {
    state.attractorScale = parseFloat(e.target.value);
    document.getElementById('attractor-scale-val').textContent = state.attractorScale.toFixed(2);
    const isAttractor = ['pickover', 'dejong', 'tinkerbell', 'lorenz'].includes(state.fractalType);
    if (isAttractor) updateAttractorPoints();
});

document.getElementById('power').addEventListener('input', (e) => {
    state.power = parseInt(e.target.value);
    document.getElementById('power-val').textContent = state.power;
    material.uniforms.power.value = state.power;
});

document.getElementById('rotation-speed').addEventListener('input', (e) => {
    state.rotationSpeed = parseFloat(e.target.value);
    document.getElementById('rotation-val').textContent = state.rotationSpeed.toFixed(1);
});

document.getElementById('toggle-view').addEventListener('click', () => {
    state.is3D = !state.is3D;
    const btn = document.getElementById('toggle-view');
    btn.textContent = state.is3D ? 'Switch to 2D View' : 'Switch to 3D View';

    if (!state.is3D) {
        // Reset camera for 2D view
        camera.position.set(0, 0, -3);
        camera.lookAt(0, 0, 0);
        controls.enableRotate = false;
        fractalMesh.rotation.set(0, 0, 0);
        state.rotationSpeed = 0;
        document.getElementById('rotation-speed').value = 0;
        document.getElementById('rotation-val').textContent = '0.0';
    } else {
        controls.enableRotate = true;
    }

    const isAttractor = ['pickover', 'dejong', 'tinkerbell', 'lorenz'].includes(state.fractalType);
    if (isAttractor) {
        updateAttractorPoints();
    }
});

document.getElementById('reset-params').addEventListener('click', () => {
    state.iterations = 8;
    state.zoom = 2.0;
    state.power = 8;
    state.rotationSpeed = 0.3;
    state.juliaX = 0.3;
    state.juliaY = 0.5;
    state.attractorA = 1.0;
    state.attractorB = 1.8;
    state.attractorC = 0.7;
    state.attractorD = 1.2;
    state.attractorScale = 1.0;
    state.lorenzSigma = 10.0;
    state.lorenzRho = 28.0;
    state.lorenzBeta = 2.67;
    state.is3D = true;

    // Reset UI
    document.getElementById('iterations').value = 8;
    document.getElementById('iter-val').textContent = '8';
    document.getElementById('zoom').value = 2.0;
    document.getElementById('zoom-val').textContent = '2.0';
    document.getElementById('power').value = 8;
    document.getElementById('power-val').textContent = '8';
    document.getElementById('rotation-speed').value = 0.3;
    document.getElementById('rotation-val').textContent = '0.3';
    document.getElementById('julia-x').value = 0.3;
    document.getElementById('julia-x-val').textContent = '0.30';
    document.getElementById('julia-y').value = 0.5;
    document.getElementById('julia-y-val').textContent = '0.50';
    document.getElementById('toggle-view').textContent = 'Switch to 2D View';

    document.getElementById('attractor-a').value = 1.0;
    document.getElementById('attractor-a-val').textContent = '1.00';
    document.getElementById('attractor-b').value = 1.8;
    document.getElementById('attractor-b-val').textContent = '1.80';
    document.getElementById('attractor-c').value = 0.7;
    document.getElementById('attractor-c-val').textContent = '0.70';
    document.getElementById('attractor-d').value = 1.2;
    document.getElementById('attractor-d-val').textContent = '1.20';
    document.getElementById('attractor-scale').value = 1.0;
    document.getElementById('attractor-scale-val').textContent = '1.0';
    
    document.getElementById('lorenz-sigma').value = 10.0;
    document.getElementById('lorenz-sigma-val').textContent = '10.0';
    document.getElementById('lorenz-rho').value = 28.0;
    document.getElementById('lorenz-rho-val').textContent = '28.0';
    document.getElementById('lorenz-beta').value = 2.67;
    document.getElementById('lorenz-beta-val').textContent = '2.67';

    // Reset uniforms
    material.uniforms.maxIterations.value = 8;
    material.uniforms.zoom.value = 2.0;
    material.uniforms.power.value = 8;
    material.uniforms.juliaC.value.set(0.3, 0.5);

    // Reset camera and controls
    camera.position.set(0, 0, -3);
    camera.lookAt(0, 0, 0);
    controls.enableRotate = true;
    fractalMesh.rotation.set(0, 0, 0);

    const isAttractor = ['pickover', 'dejong', 'tinkerbell', 'lorenz'].includes(state.fractalType);
    if (isAttractor) updateAttractorPoints();
});

document.getElementById('particle-shape').addEventListener('change', (e) => {
    pickoverMaterial.map = particleTextures[e.target.value];
    pickoverMaterial.needsUpdate = true;
});

document.getElementById('fullscreen-btn').addEventListener('click', () => {
    const container = document.getElementById('container');
    if (!document.fullscreenElement) {
        container.requestFullscreen().catch(err => {
            console.warn(`Error attempting to enable fullscreen: ${err.message}`);
        });
    }
});

document.getElementById('exit-fullscreen-btn').addEventListener('click', () => {
    if (document.exitFullscreen) {
        document.exitFullscreen();
    }
});

document.addEventListener('fullscreenchange', () => {
    const panels = document.querySelectorAll('.panel');
    const exitBtn = document.getElementById('exit-fullscreen-btn');
    
    if (document.fullscreenElement) {
        panels.forEach(p => p.style.display = 'none');
        exitBtn.style.display = 'block';
    } else {
        panels.forEach(p => p.style.display = 'block');
        exitBtn.style.display = 'none';
        updateUIVisibility();
    }
});

// Initial math info
updateMathInfo();

// ============================================
// Responsive Canvas
// ============================================
window.addEventListener('resize', () => {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
});

// ============================================
// Animation Loop
// ============================================
function animate() {
    requestAnimationFrame(animate);

    state.time += 0.016;
    material.uniforms.time.value = state.time;
    material.uniforms.cameraPos.value.copy(camera.position);

    const rotMatrix = new THREE.Matrix4();
    rotMatrix.extractRotation(camera.matrixWorld);
    material.uniforms.cameraRotation.value.copy(rotMatrix);

    // Auto-rotation
    if (state.rotationSpeed > 0) {
        fractalMesh.rotation.y += state.rotationSpeed * 0.01;
    }

    controls.update();
    renderer.render(scene, camera);
}

animate();

console.log('3D Fractal Viewer initialized!');
