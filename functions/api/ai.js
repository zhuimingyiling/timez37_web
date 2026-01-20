export async function onRequestPost(context) {
    try {
        const { tableData, textData, prompt } = await context.request.json();
        
        // 1. 设置 API KEY (建议在 Cloudflare 后台 Variables 设置)
        const API_KEY = context.env.API_KEY || "这里填入你的_Google_Gemini_API_Key";
        
        // 2. 准备数据上下文
        let dataContent = "";
        if (tableData && tableData.length > 0) {
            // Gemini 对 JSON 的理解能力极强，直接发 JSON 数组即可
            dataContent = JSON.stringify(tableData.slice(0, 100)); 
        } else {
            dataContent = textData || "暂无数据";
        }

        // 3. 调用 Google Gemini API
        const response = await fetch(`https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key=${API_KEY}`, {
            method: "POST",
            headers: {
                "Content-Type": "application/json"
            },
            body: JSON.stringify({
                contents: [{
                    parts: [{
                        text: `你是一个专业的数据分析专家。以下是提供的数据内容：\n${dataContent}\n\n请根据以上数据回答用户的问题：${prompt}`
                    }]
                }],
                generationConfig: {
                    temperature: 0.7,
                    maxOutputTokens: 2048,
                }
            })
        });

        const result = await response.json();

        // 4. 解析 Gemini 的返回格式
        if (result.candidates && result.candidates[0]) {
            const aiMsg = result.candidates[0].content.parts[0].text;
            return new Response(JSON.stringify({ response: aiMsg }));
        } else {
            return new Response(JSON.stringify({ response: "Gemini 未能生成回复，请检查 API 状态。" }));
        }

    } catch (err) {
        return new Response(JSON.stringify({ response: "Gemini 对接报错: " + err.message }));
    }
}
