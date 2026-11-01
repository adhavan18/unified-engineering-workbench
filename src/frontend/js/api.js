// thin client for the c++ backend. every computation happens server-side;
// this module only moves bytes and turns errors into readable messages.

export class ApiError extends Error {
  constructor(message, { hint = '', status = 0, line = 0 } = {}) {
    super(message);
    this.hint = hint;
    this.status = status;
    this.line = line;
  }
}

async function send(url, options = {}) {
  let res;
  try {
    res = await fetch(url, options);
  } catch {
    throw new ApiError('the backend is not reachable', {
      hint: 'Launch.exe may have been closed. start it again and reload this page.',
    });
  }
  if (!res.ok) {
    let body = {};
    try { body = await res.json(); } catch { /* not json */ }
    throw new ApiError(body.error || `request failed (http ${res.status})`, {
      hint: body.hint || '', status: res.status, line: body.line || 0,
    });
  }
  return res;
}

export async function getJson(url) {
  return (await send(url)).json();
}

export async function postJson(url) {
  return (await send(url, { method: 'POST' })).json();
}

export async function getText(url) {
  const res = await send(url);
  return { text: await res.text(), name: res.headers.get('X-File-Name') || '' };
}

// uploads a File/Blob/string as the raw request body and decodes the binary reply.
export async function postBinary(url, body) {
  const res = await send(url, { method: 'POST', body, headers: { 'Content-Type': 'application/octet-stream' } });
  return decodeBinary(await res.arrayBuffer());
}

// "WBF1" | u32 json length | json | pad to 8 | typed arrays (see server/api.hpp)
export function decodeBinary(buffer) {
  const view = new DataView(buffer);
  const magic = String.fromCharCode(view.getUint8(0), view.getUint8(1), view.getUint8(2), view.getUint8(3));
  if (magic !== 'WBF1') throw new ApiError('unexpected reply from the backend');
  const len = view.getUint32(4, true);
  const meta = JSON.parse(new TextDecoder().decode(new Uint8Array(buffer, 8, len)));
  const start = Math.ceil((8 + len) / 8) * 8;
  const arrays = {};
  for (const [name, a] of Object.entries(meta.arrays)) {
    const Ctor = a.type === 'f64' ? Float64Array : Float32Array;
    arrays[name] = new Ctor(buffer, start + a.offset, a.count);
  }
  return { meta, arrays };
}

// starts a background job and polls until it finishes, fails or is cancelled.
export async function runJob(startUrl, onProgress) {
  const { job } = await postJson(startUrl);
  for (;;) {
    const s = await getJson(`/api/jobs/${encodeURIComponent(job)}`);
    onProgress?.(s);
    if (s.state === 'done') return s;
    if (s.state === 'failed') throw new ApiError(s.error || 'the job failed', { hint: 'see the activity log' });
    if (s.state === 'cancelled') return s;
    await new Promise((r) => setTimeout(r, 150));
  }
}

export function cancelJob(id) {
  return postJson(`/api/jobs/${encodeURIComponent(id)}/cancel`);
}

// triggers a browser download of a backend-generated file.
export function download(url) {
  const a = document.createElement('a');
  a.href = url;
  a.download = '';
  document.body.appendChild(a);
  a.click();
  a.remove();
}

export function query(params) {
  return new URLSearchParams(params).toString();
}
