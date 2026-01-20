export async function onRequestPost(context) {
    const { tableData, prompt } = await context.request.json();

    // 调用 Cloudflare 内置的 AI 模型 (Llama-3)
    // 修改后的 api/ai.js 核心逻辑
const { tableData, textData, prompt } = await context.request.json();
const content = tableData ? JSON.stringify(tableData) : textData;

const answer = await context.env.AI.run('@cf/meta/llama-3-8b-instruct', {
    messages: [
        { role: 'user', content: `请根据以下内容回答问题：${content} \n 问题：${prompt}` }
    ]
});
