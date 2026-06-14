type SubmitBody = {
  username?: string;
  token?: string;
  payload?: Record<string, unknown>;
};

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
};

function jsonResponse(body: Record<string, unknown>, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: {
      ...corsHeaders,
      "Content-Type": "application/json",
      "Connection": "keep-alive",
    },
  });
}

function secretKey(): string {
  const modern = Deno.env.get("SUPABASE_SECRET_KEYS");
  if (modern) {
    try {
      const parsed = JSON.parse(modern);
      if (parsed?.default) return parsed.default;
    } catch {
      // Fall back to legacy secret below.
    }
  }
  return Deno.env.get("SUPABASE_SERVICE_ROLE_KEY") || "";
}

async function callRpc(functionName: string, body: Record<string, unknown>): Promise<Record<string, unknown>> {
  const supabaseUrl = Deno.env.get("SUPABASE_URL") || "";
  const key = secretKey();
  if (!supabaseUrl || !key) {
    return { ok: false, error: "Supabase Edge Function mangler standard secrets." };
  }

  const response = await fetch(`${supabaseUrl}/rest/v1/rpc/${functionName}`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "Accept": "application/json",
      "apikey": key,
      "Authorization": `Bearer ${key}`,
    },
    body: JSON.stringify(body),
  });

  const text = await response.text();
  let parsed: Record<string, unknown> = {};
  try {
    parsed = text ? JSON.parse(text) : {};
  } catch {
    parsed = { error: text };
  }

  if (!response.ok) {
    return {
      ok: false,
      error: String(parsed.error || parsed.message || response.statusText),
    };
  }

  return parsed;
}

function encodeBase64(value: string): string {
  const bytes = new TextEncoder().encode(value);
  let binary = "";
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary);
}

async function markQueueStatus(queueId: string, status: "sent" | "failed", error = "") {
  await callRpc("provi_mark_sales_registration_status", {
    p_queue_id: queueId,
    p_status: status,
    p_error: error || null,
  });
}

async function sendViaResend(queueId: string, payload: Record<string, unknown>) {
  const apiKey = Deno.env.get("RESEND_API_KEY") || "";
  const from = Deno.env.get("SALES_REGISTRATION_FROM") || "";
  const recipient = String(payload.recipient || "");

  if (!apiKey || !from) {
    return {
      ok: true,
      mailed: false,
      mailerConfigured: false,
      message: "Salgs-reg er lagt i online kø. Mailer mangler RESEND_API_KEY og SALES_REGISTRATION_FROM.",
    };
  }

  const subject = String(payload.mailSubject || "Salgs reg - Provi Tracker");
  const html = [
    "<p>Salgsregistrering fra Provi Tracker.</p>",
    String(payload.mailHtml || ""),
    "<p>JSON-data er vedhæftet til Power Automate-mailflowet.</p>",
  ].join("");
  const payloadJson = JSON.stringify(payload, null, 2);

  const response = await fetch("https://api.resend.com/emails", {
    method: "POST",
    headers: {
      "Authorization": `Bearer ${apiKey}`,
      "Content-Type": "application/json",
    },
    body: JSON.stringify({
      from,
      to: [recipient],
      subject,
      html,
      attachments: [
        {
          filename: "sales-registration.json",
          content: encodeBase64(payloadJson),
        },
      ],
    }),
  });

  const text = await response.text();
  if (!response.ok) {
    await markQueueStatus(queueId, "failed", text || response.statusText);
    return {
      ok: false,
      mailed: false,
      mailerConfigured: true,
      error: text || response.statusText,
    };
  }

  await markQueueStatus(queueId, "sent");
  return {
    ok: true,
    mailed: true,
    mailerConfigured: true,
    message: "Salgs-reg sendt online til mailflow.",
  };
}

Deno.serve(async (req: Request) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }

  if (req.method !== "POST") {
    return jsonResponse({ ok: false, error: "Kun POST er understøttet." }, 405);
  }

  let body: SubmitBody;
  try {
    body = await req.json();
  } catch {
    return jsonResponse({ ok: false, error: "Ugyldigt JSON input." }, 400);
  }

  const payload = body.payload || {};
  const enqueue = await callRpc("provi_enqueue_sales_registration", {
    p_username: body.username || "",
    p_token: body.token || "",
    p_payload: payload,
  });

  if (!enqueue.ok) {
    return jsonResponse({ ok: false, error: enqueue.error || "Salgs-reg kunne ikke lægges i online kø." }, 400);
  }

  const queueId = String(enqueue.queue_id || "");
  const mailResult = await sendViaResend(queueId, payload);

  return jsonResponse({
    ok: Boolean(mailResult.ok),
    error: mailResult.ok ? "" : mailResult.error || "Salgs-reg mailer fejlede.",
    data: {
      queueId,
      queued: true,
      mailed: Boolean(mailResult.mailed),
      mailerConfigured: Boolean(mailResult.mailerConfigured),
      message: mailResult.message || "",
    },
  }, mailResult.ok ? 200 : 502);
});
