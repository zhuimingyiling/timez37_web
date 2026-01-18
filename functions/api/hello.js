export async function onRequest() {
  return new Response(JSON.stringify({ msg: "来自云端的握手！" }));
}
