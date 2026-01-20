export async function onRequestPost(context) {
    const { tableData, prompt } = await context.request.json();

    // 调用 Cloudflare 内置的 AI 模型 (Llama-3)
    const answer = await context.env.AI.run('@cf/meta/llama-3-8b-instruct', {
        messages: [
            { role: 'system', content: '你是一个数据分析专家，请根据提供的表格数据回答问题。' },
            { role: 'user', content: `数据内容：${JSON.stringify(tableData)} \n\n 问题：${prompt}` }
        ]
    });

    return new Response(JSON.stringify(answer));
}
